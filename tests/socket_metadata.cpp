// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <atomic>
#include <barrier>
#include <future>
#include <string>
#include <thread>
#include <type_traits>

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <arknet/rudp/rudp_server.hpp>
#include <arknet/tcp/tcp_server.hpp>
#include <arknet/udp/udp_server.hpp>
#include "check.hpp"

namespace {

template<class Server>
Server make_server() {
    return Server(arknet::detail::udp_frame_size, arknet::detail::max_buffer_size, 1);
}

template<class Server>
void check_closed(const Server& server) {
    CHECK(server.get_listen_port() == 0);
    CHECK(arknet::get_last_error() == asio::error::bad_descriptor);
    CHECK(server.get_listen_address().empty());
    CHECK(arknet::get_last_error() == asio::error::bad_descriptor);
}

// Used only from serialized IO callbacks: compare with native platform
// behavior, including Winsock's error for an opened but not-yet-bound socket.
template<class Server>
void check_native_snapshot(Server& server) {
    arknet::error_code error;
    const auto endpoint = server.acceptor().local_endpoint(error);
    CHECK(server.get_listen_port() == endpoint.port());
    CHECK(arknet::get_last_error() == error);
    CHECK(server.get_listen_address() == (error ? std::string{} : endpoint.address().to_string()));
    CHECK(arknet::get_last_error() == error);
}

template<class Server>
void lifecycle() {
    auto server = make_server<Server>();
    check_closed(server);
    unsigned initialized = 0;
    unsigned started = 0;
    unsigned stopped = 0;
    unsigned short bound_port = 0;
    server.bind_init([&] {
        ++initialized;
        check_native_snapshot(server);
        CHECK(server.get_listen_port() == 0);
    });
    server.bind_start([&] {
        ++started;
        const auto result = arknet::get_last_error();
        check_native_snapshot(server);
        const auto port = server.get_listen_port();
        if (!result)
        {
            CHECK(port != 0);
            CHECK(server.get_listen_address() == "127.0.0.1");
        }
        // Reading metadata preserves the getter's established last_error
        // behavior; restore the start result for the enclosing start operation.
        arknet::set_last_error(result);
    });
    server.bind_stop([&] {
        ++stopped;
        if constexpr (std::is_same_v<Server, arknet::tcp_server>)
        {
            if (bound_port)
            {
                CHECK(server.get_listen_port() == bound_port);
                CHECK(server.get_listen_address() == "127.0.0.1");
            }
        }
        else
        {
            check_closed(server);
        }
    });
    REQUIRE(server.start("127.0.0.1", 0));
    CHECK(initialized == 1);
    CHECK(started == 1);
    bound_port = server.get_listen_port();
    CHECK(bound_port != 0);
    CHECK(server.listen_port() == bound_port);
    CHECK_FALSE(arknet::get_last_error());
    CHECK(server.listen_address() == "127.0.0.1");
    CHECK_FALSE(arknet::get_last_error());
    server.stop();
    CHECK(stopped == 1);
    check_closed(server);
    bound_port = 0;

    using protocol = std::conditional_t<std::is_same_v<Server, arknet::tcp_server>, asio::ip::tcp, asio::ip::udp>;
    using blocker_type =
        std::conditional_t<std::is_same_v<Server, arknet::tcp_server>, asio::ip::tcp::acceptor, asio::ip::udp::socket>;
    asio::io_context context;
    blocker_type blocker(context);
    blocker.open(protocol::v4());
#if defined(_WIN32)
    // A later SO_REUSEADDR bind may otherwise override an ordinary UDP bind.
    const int exclusive = 1;
    REQUIRE(::setsockopt(blocker.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == 0);
#endif
    blocker.bind(typename protocol::endpoint(asio::ip::address_v4::loopback(), 0));
    CHECK_FALSE(server.start("127.0.0.1", blocker.local_endpoint().port()));
    server.stop();
    CHECK(initialized == 2);
    CHECK(started == 2);
    CHECK(stopped == 2);
    check_closed(server);

    CHECK_FALSE(server.start("127.0.0.1", "invalid/service"));
    server.stop();
    CHECK(initialized == 2);
    CHECK(started == 3);
    CHECK(stopped == 3);
    check_closed(server);
}

struct external_runtime
{
    asio::io_context context;
    asio::executor_work_guard<asio::io_context::executor_type> work{context.get_executor()};
    std::array<std::thread, 2> workers;

    external_runtime() {
        for (auto& worker : workers)
        {
            worker = std::thread([this] { context.run(); });
        }
    }

    ~external_runtime() {
        context.stop();
        for (auto& worker : workers)
        {
            worker.join();
        }
    }
};

template<class Server>
void callback_metadata() {
    external_runtime runtime;
    Server server(runtime.context);
    std::promise<void> stopped;
    auto completion = stopped.get_future();
    unsigned short port = 0;
    server.bind_init([&] {
        check_native_snapshot(server);
        CHECK(server.get_listen_port() == 0);
    });
    server.bind_start([&] {
        CHECK_FALSE(arknet::get_last_error());
        port = server.get_listen_port();
        CHECK(port != 0);
        CHECK(server.get_listen_address() == "127.0.0.1");
        server.stop();
    });
    server.bind_stop([&] {
        if constexpr (std::is_same_v<Server, arknet::tcp_server>)
        {
            CHECK(server.get_listen_port() == port);
            CHECK(server.get_listen_address() == "127.0.0.1");
        }
        else
        {
            check_closed(server);
        }
        stopped.set_value();
    });
    REQUIRE(server.start("127.0.0.1", 0));
    await(completion);
    server.stop();
    check_closed(server);
    CHECK_FALSE(runtime.context.stopped());
}

template<class Server>
void concurrent_stop() {
    auto server = make_server<Server>();
    REQUIRE(server.start("127.0.0.1", 0));
    const auto port = server.get_listen_port();
    REQUIRE(port != 0);
    std::barrier ready(5);
    std::atomic<bool> finished{};
    std::atomic<unsigned> invalid{};
    std::atomic<unsigned> reads{};
    std::array<std::thread, 4> readers;
    for (auto& reader : readers)
    {
        reader = std::thread([&] {
            ready.arrive_and_wait();
            while (!finished.load())
            {
                const auto observed_port = server.get_listen_port();
                const auto port_error = arknet::get_last_error();
                if ((port_error && port_error != asio::error::bad_descriptor &&
                        port_error != asio::error::invalid_argument) ||
                    (port_error && observed_port != 0) || (observed_port != 0 && observed_port != port))
                {
                    ++invalid;
                }
                const auto address = server.get_listen_address();
                const auto address_error = arknet::get_last_error();
                if ((address_error && address_error != asio::error::bad_descriptor &&
                        address_error != asio::error::invalid_argument) ||
                    (address_error && !address.empty()) ||
                    (!address_error && address != "127.0.0.1" && address != "0.0.0.0"))
                {
                    ++invalid;
                }
                ++reads;
            }
        });
    }
    ready.arrive_and_wait();
    unsigned successful_restarts = 0;
    for (unsigned iteration = 0; iteration < 8; ++iteration)
    {
        server.stop();
        if (server.start("127.0.0.1", port))
        {
            ++successful_restarts;
        }
    }
    server.stop();
    finished = true;
    for (auto& reader : readers)
    {
        reader.join();
    }
    CHECK(successful_restarts == 8);
    CHECK(reads.load() != 0);
    CHECK(invalid.load() == 0);
    check_closed(server);
}

} // namespace

DOCTEST_TEST_CASE("TCP listen metadata follows open bind failure and close") {
    lifecycle<arknet::tcp_server>();
}

DOCTEST_TEST_CASE("UDP listen metadata follows open bind failure and close") {
    lifecycle<arknet::udp_server>();
}

DOCTEST_TEST_CASE("RUDP listen metadata follows open bind failure and close") {
    lifecycle<arknet::rudp_server>();
}

DOCTEST_TEST_CASE("TCP metadata reads may overlap IO-owned stop and restart") {
    concurrent_stop<arknet::tcp_server>();
}

DOCTEST_TEST_CASE("UDP metadata reads may overlap IO-owned stop and restart") {
    concurrent_stop<arknet::udp_server>();
}

DOCTEST_TEST_CASE("RUDP metadata reads may overlap IO-owned stop and restart") {
    concurrent_stop<arknet::rudp_server>();
}

DOCTEST_TEST_CASE("TCP metadata is available in external-context lifecycle callbacks") {
    callback_metadata<arknet::tcp_server>();
}

DOCTEST_TEST_CASE("UDP metadata is available in external-context lifecycle callbacks") {
    callback_metadata<arknet::udp_server>();
}

DOCTEST_TEST_CASE("RUDP metadata is available in external-context lifecycle callbacks") {
    callback_metadata<arknet::rudp_server>();
}
