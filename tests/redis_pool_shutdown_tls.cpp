// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <arknet/redis/pool.hpp>
#include "check.hpp"
#include "redis_test_peer.hpp"

namespace {

using namespace std::chrono_literals;
using arknet::redis::client_errc;
using arknet::redis::result;

struct tracked_tls_transport : arknet::tcps_client
{
    inline static std::atomic<unsigned> contexts_destroyed{};
    inline static std::atomic<unsigned> streams_destroyed{};
    inline static std::atomic<unsigned> transports_destroyed{};

    static void record_free(void*, void* pointer, CRYPTO_EX_DATA*, int, long, void*) {
        if (pointer)
        {
            ++*static_cast<std::atomic<unsigned>*>(pointer);
        }
    }

    static int context_index() {
        static const auto index = SSL_CTX_get_ex_new_index(0, nullptr, nullptr, nullptr, record_free);
        return index;
    }

    static int stream_index() {
        static const auto index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, record_free);
        return index;
    }

    template<class... construction>
    explicit tracked_tls_transport(construction&&... arguments)
        : arknet::tcps_client(std::forward<construction>(arguments)...) {
        if (context_index() < 0 || SSL_CTX_set_ex_data(native_handle(), context_index(), &contexts_destroyed) != 1)
        {
            throw std::runtime_error("Could not register TLS context destruction probe");
        }
        bind_handshake([this] {
            if (stream_index() < 0 ||
                SSL_set_ex_data(stream().native_handle(), stream_index(), &streams_destroyed) != 1)
            {
                throw std::runtime_error("Could not register TLS stream destruction probe");
            }
        });
    }

    ~tracked_tls_transport() {
        ++transports_destroyed;
    }
};

class encrypted_peer
{
    asio::ssl::stream<asio::ip::tcp::socket>& stream_;
    arknet::redis::detail::resp_decoder decoder_;

public:
    explicit encrypted_peer(asio::ssl::stream<asio::ip::tcp::socket>& stream)
        : stream_(stream) {
        stream_.next_layer().non_blocking(true);
    }

    arknet::redis::command read_command() {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            arknet::redis::value reply;
            if (decoder_.next(reply) == arknet::redis::detail::resp_status::ready)
            {
                if (reply.type != arknet::redis::value_type::array)
                {
                    throw std::runtime_error("TLS peer expected a RESP command array");
                }
                arknet::redis::command command;
                for (auto& value : reply.elements)
                {
                    command.push_back(std::move(value.text));
                }
                return command;
            }
            std::array<char, 4096> bytes;
            arknet::error_code error;
            const auto count = stream_.read_some(asio::buffer(bytes), error);
            if (error == asio::error::would_block || error == asio::error::try_again)
            {
                std::this_thread::sleep_for(1ms);
                continue;
            }
            if (error)
            {
                throw arknet::system_error(error);
            }
            if (!decoder_.append(std::string_view(bytes.data(), count)))
            {
                throw std::runtime_error("TLS peer exceeded RESP decode limits");
            }
        }
        throw std::runtime_error("TLS peer command timeout");
    }

    void send(std::string_view bytes) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!bytes.empty() && std::chrono::steady_clock::now() < deadline)
        {
            arknet::error_code error;
            const auto count = stream_.write_some(asio::buffer(bytes), error);
            bytes.remove_prefix(count);
            if (error == asio::error::would_block || error == asio::error::try_again)
            {
                std::this_thread::sleep_for(1ms);
            }
            else if (error)
            {
                throw arknet::system_error(error);
            }
        }
        if (!bytes.empty())
        {
            throw std::runtime_error("TLS peer send timeout");
        }
    }

    void wait_closed() {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::array<char, 128> bytes;
            arknet::error_code error;
            stream_.read_some(asio::buffer(bytes), error);
            if (error && error != asio::error::would_block && error != asio::error::try_again)
            {
                return;
            }
            std::this_thread::sleep_for(1ms);
        }
        throw std::runtime_error("TLS peer did not observe connection close");
    }
};

class encrypted_server
{
    asio::io_context context_;
    asio::ssl::context tls_{asio::ssl::context::tls_server};
    asio::ip::tcp::acceptor acceptor_{context_, {asio::ip::address_v4::loopback(), 0}};
    std::promise<void> completion_;
    std::future<void> finished_ = completion_.get_future();
    std::thread worker_;

public:
    template<class script_type>
    explicit encrypted_server(script_type script) {
        tls_.use_certificate_chain_file(std::string(ARKNET_TEST_CERT_DIR) + "/server.pem");
        tls_.use_private_key_file(std::string(ARKNET_TEST_CERT_DIR) + "/server-key.pem", asio::ssl::context::pem);
        worker_ = std::thread([this, script = std::move(script)]() mutable {
            try
            {
                asio::ssl::stream<asio::ip::tcp::socket> socket(context_, tls_);
                asio::steady_timer deadline(context_, 5s);
                deadline.async_wait([&](arknet::error_code error) {
                    if (!error)
                    {
                        arknet::error_code ignored;
                        acceptor_.cancel(ignored);
                        socket.next_layer().close(ignored);
                    }
                });
                asio::co_spawn(
                    context_,
                    [&]() -> asio::awaitable<void> {
                        co_await acceptor_.async_accept(socket.next_layer(), asio::use_awaitable);
                        co_await socket.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);
                        deadline.cancel();
                        encrypted_peer peer(socket);
                        script(peer);
                    },
                    [&](std::exception_ptr error) {
                        deadline.cancel();
                        if (error)
                        {
                            completion_.set_exception(error);
                        }
                        else
                        {
                            completion_.set_value();
                        }
                    });
                context_.run();
            }
            catch (...)
            { completion_.set_exception(std::current_exception()); }
        });
    }

    ~encrypted_server() {
        worker_.join();
    }

    std::string port() const {
        return std::to_string(acceptor_.local_endpoint().port());
    }

    void finish() {
        await(finished_);
    }
};

} // namespace

DOCTEST_TEST_CASE("Redis pool shutdown frees retained TLS child contexts and streams before success") {
    std::promise<void> sent;
    auto active = sent.get_future();
    encrypted_server endpoint([&](encrypted_peer& peer) {
        CHECK(peer.read_command() == arknet::redis::command{"HELLO", "3"});
        peer.send("%1\r\n+proto\r\n:3\r\n");
        CHECK(peer.read_command() == arknet::redis::command{"PING"});
        sent.set_value();
        peer.wait_closed();
    });
    using transport = tracked_tls_transport;
    using pool = arknet::redis::basic_pool<transport>;
    const auto contexts = transport::contexts_destroyed.load();
    const auto streams = transport::streams_destroyed.load();
    const auto transports = transport::transports_destroyed.load();
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    options.certificate_authority = std::string(ARKNET_TEST_CERT_DIR) + "/ca.pem";
    pool connections(options);
    std::promise<pool::acquire_result> connected;
    auto ready = connected.get_future();
    connections.async_acquire([&](auto response) { connected.set_value(std::move(response)); });
    auto leased = await(ready);
    REQUIRE(leased);
    result response;
    unsigned completions{};
    leased.connection.async_execute({"PING"}, [&](result value) {
        response = std::move(value);
        ++completions;
    });
    REQUIRE(active.wait_for(5s) == std::future_status::ready);
    CHECK(connections.close_and_wait());
    CHECK(response.error == client_errc::closed);
    CHECK(response.command_may_have_run);
    CHECK(completions == 1);
    CHECK(transport::streams_destroyed == streams + 1);
    CHECK(transport::contexts_destroyed == contexts + 1);
    CHECK(transport::transports_destroyed == transports + 1);
    CHECK_FALSE(leased.connection);
    CHECK(leased.connection.async_execute({"PING"}, {}) == 0);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool shutdown retires an incomplete TLS handshake before releasing its runtime") {
    std::promise<void> accepted;
    auto pending = accepted.get_future();
    redis_test::server endpoint([&](redis_test::peer& peer) {
        accepted.set_value();
        peer.wait_closed();
    });
    using transport = tracked_tls_transport;
    using pool = arknet::redis::basic_pool<transport>;
    const auto contexts = transport::contexts_destroyed.load();
    const auto streams = transport::streams_destroyed.load();
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    options.certificate_authority = std::string(ARKNET_TEST_CERT_DIR) + "/ca.pem";
    pool connections(options);
    unsigned completed{};
    connections.async_acquire([&](auto response) {
        CHECK(response.error == client_errc::closed);
        ++completed;
    });
    REQUIRE(pending.wait_for(5s) == std::future_status::ready);
    CHECK(connections.close_and_wait());
    CHECK(completed == 1);
    CHECK(transport::streams_destroyed == streams + 1);
    CHECK(transport::contexts_destroyed == contexts + 1);
    endpoint.finish();
}
