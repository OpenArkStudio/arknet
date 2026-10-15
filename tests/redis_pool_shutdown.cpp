// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <arknet/redis/pool.hpp>
#include "redis_test_peer.hpp"

namespace {

using namespace redis_test;
using namespace std::chrono_literals;

struct publication_probe
{
    std::latch entered{1};
    std::latch release{1};
};

struct tracked_transport : arknet::tcp_client
{
    template<class... Args>
    explicit tracked_transport(Args&&... args)
        : arknet::tcp_client(std::forward<Args>(args)...) {
        ++constructed;
    }

    inline static std::atomic<unsigned> constructed{};
    inline static std::atomic<unsigned> destroyed{};
    inline static std::atomic<unsigned> late_connect_errors{};
    inline static std::atomic<publication_probe*> probe{};
    inline static std::atomic<publication_probe*> connect_probe{};
    inline static std::function<void()> reenter;
    inline static std::function<void()> on_destroy;
    inline static std::function<void(arknet::error_code)> before_connect;
    inline static std::mutex callbacks_mutex;
    std::atomic_bool stop_requested{};

    template<class Handler>
    auto& bind_connect(Handler handler) {
        arknet::tcp_client::bind_connect([this, handler = std::move(handler)]() mutable {
            const auto error = arknet::get_last_error();
            std::function<void(arknet::error_code)> callback;
            {
                std::lock_guard lock(callbacks_mutex);
                callback = std::exchange(before_connect, {});
            }
            if (callback)
            {
                callback(error);
            }
            if (stop_requested && error)
            {
                ++late_connect_errors;
            }
            arknet::set_last_error(error);
            handler();
        });
        return *this;
    }

    template<class Host, class Port>
    bool async_start(Host&& host, Port&& port) {
        const auto started = arknet::tcp_client::async_start(std::forward<Host>(host), std::forward<Port>(port));
        if (auto* current = connect_probe.exchange(nullptr))
        {
            current->entered.count_down();
            current->release.wait();
        }
        return started;
    }

    void stop() {
        stop_requested = true;
        arknet::tcp_client::stop();
    }

    auto& io() {
        if (auto* current = probe.exchange(nullptr))
        {
            current->entered.count_down();
            current->release.wait();
        }
        std::function<void()> callback;
        {
            std::lock_guard lock(callbacks_mutex);
            callback = std::exchange(reenter, {});
        }
        if (callback)
        {
            callback();
        }
        return arknet::tcp_client::io();
    }

    ~tracked_transport() {
        std::function<void()> callback;
        {
            std::lock_guard lock(callbacks_mutex);
            callback = std::exchange(on_destroy, {});
        }
        if (callback)
        {
            callback();
        }
        ++destroyed;
    }
};

using tracked_pool = arknet::redis::basic_pool<tracked_transport>;
using pool = arknet::redis::pool;

template<class Pool>
auto acquire(Pool& connections) {
    std::promise<typename Pool::acquire_result> completed;
    auto future = completed.get_future();
    connections.async_acquire([&](auto response) { completed.set_value(std::move(response)); });
    return await(future);
}

template<class Lease>
result execute(Lease& connection, command arguments) {
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_execute(std::move(arguments), [&](result response) { completed.set_value(std::move(response)); });
    return await(future);
}

struct capture_probe
{
    std::atomic<unsigned>& destroyed;

    ~capture_probe() {
        ++destroyed;
    }
};

struct worker_exit_probe
{
    std::shared_ptr<std::atomic<bool>> exited;

    ~worker_exit_probe() {
        if (exited)
        {
            *exited = true;
        }
    }
};

void record_worker_exit(std::shared_ptr<std::atomic<bool>> exited) {
    thread_local worker_exit_probe probe;
    probe.exited = std::move(exited);
}

class paired_server
{
    asio::io_context context_;
    asio::ip::tcp::acceptor acceptor_{context_, {asio::ip::address_v4::loopback(), 0}};
    std::thread worker_;
    std::promise<void> completion_;
    std::future<void> finished_ = completion_.get_future();

public:
    paired_server() {
        acceptor_.non_blocking(true);
        worker_ = std::thread([this] {
            std::vector<std::thread> peers;
            std::mutex failures_mutex;
            std::exception_ptr failure;
            try
            {
                for (unsigned index = 0; index < 2; ++index)
                {
                    asio::ip::tcp::socket socket(context_);
                    const auto deadline = std::chrono::steady_clock::now() + 5s;
                    for (;;)
                    {
                        arknet::error_code error;
                        acceptor_.accept(socket, error);
                        if (!error)
                        {
                            break;
                        }
                        if ((error != asio::error::would_block && error != asio::error::try_again) ||
                            std::chrono::steady_clock::now() >= deadline)
                        {
                            throw std::runtime_error("Two-child Redis peer accept failed");
                        }
                        std::this_thread::sleep_for(1ms);
                    }
                    peers.emplace_back([&, socket = std::move(socket)]() mutable {
                        try
                        {
                            peer remote(socket);
                            remote.hello();
                            remote.expect({"PING"});
                            remote.send("+PONG\r\n");
                            remote.wait_closed();
                        }
                        catch (...)
                        {
                            std::lock_guard lock(failures_mutex);
                            failure = std::current_exception();
                        }
                    });
                }
            }
            catch (...)
            {
                std::lock_guard lock(failures_mutex);
                failure = std::current_exception();
            }
            for (auto& remote : peers)
            {
                remote.join();
            }
            if (failure)
            {
                completion_.set_exception(failure);
            }
            else
            {
                completion_.set_value();
            }
        });
    }

    ~paired_server() {
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

DOCTEST_TEST_CASE("Redis pool permanent shutdown joins its worker and synchronously rejects new publications") {
    pool connections;
    auto exited = std::make_shared<std::atomic<bool>>(false);
    std::promise<void> active;
    auto running = active.get_future();
    connections.async_acquire(
        [&, exited](auto response) {
            CHECK(response.error == client_errc::invalid_command);
            record_worker_exit(exited);
            active.set_value();
        },
        -1ms);
    await(running);
    CHECK(connections.close_and_wait());
    CHECK(*exited);
    CHECK(connections.close_and_wait());
    const auto caller = std::this_thread::get_id();
    unsigned completed{};
    CHECK(connections.async_acquire([&](auto response) {
        CHECK(std::this_thread::get_id() == caller);
        CHECK(response.error == client_errc::closed);
        CHECK_FALSE(response.connection);
        ++completed;
    }) == 0);
    CHECK(completed == 1);
    connections.cancel(1);
    connections.close();
    DOCTEST_CHECK_THROWS_AS(
        connections.async_acquire([](auto) { throw std::runtime_error("late rejection"); }), std::runtime_error);
}

DOCTEST_TEST_CASE("Redis pool permanent shutdown cancels pending authentication and queued acquisition once") {
    std::promise<void> hello;
    auto pending = hello.get_future();
    server endpoint([&](peer& remote) {
        remote.expect({"HELLO", "3"});
        hello.set_value();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    auto connections = std::make_unique<pool>(options);
    std::atomic<unsigned> captures{};
    auto capture = std::make_shared<capture_probe>(captures);
    unsigned completed{};
    connections->async_acquire([&, capture](auto response) {
        CHECK(response.error == client_errc::closed);
        ++completed;
        connections.reset();
    });
    connections->async_acquire([&, capture](auto response) {
        CHECK(response.error == client_errc::closed);
        ++completed;
    });
    capture.reset();
    REQUIRE(pending.wait_for(5s) == std::future_status::ready);
    CHECK(connections->close_and_wait());
    CHECK_FALSE(connections);
    CHECK(completed == 2);
    CHECK(captures == 1);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool shutdown retires active pipeline captures and retained leases without replay") {
    std::promise<void> received;
    auto active = received.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"SET", "key", "value"});
        remote.expect({"PING"});
        received.set_value();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    const auto before = tracked_transport::destroyed.load();
    tracked_pool connections(options);
    auto leased = acquire(connections);
    REQUIRE(leased);
    std::atomic<unsigned> captures{};
    auto capture = std::make_shared<capture_probe>(captures);
    result reply;
    unsigned commands{}, waiters{};
    leased.connection.async_pipeline({{"SET", "key", "value"}, {"PING"}}, [&, capture](result response) {
        reply = std::move(response);
        ++commands;
    });
    connections.async_acquire([&, capture](auto response) {
        CHECK(response.error == client_errc::closed);
        ++waiters;
    });
    capture.reset();
    REQUIRE(active.wait_for(5s) == std::future_status::ready);
    CHECK(connections.close_and_wait());
    CHECK(reply.error == client_errc::closed);
    CHECK(reply.command_may_have_run);
    CHECK(commands == 1);
    CHECK(waiters == 1);
    CHECK(captures == 1);
    CHECK(tracked_transport::destroyed == before + 1);
    CHECK_FALSE(leased.connection);
    unsigned late{};
    const auto caller = std::this_thread::get_id();
    auto rejected = [&](result response) {
        CHECK(std::this_thread::get_id() == caller);
        CHECK(response.error == client_errc::closed);
        CHECK_FALSE(response.command_may_have_run);
        ++late;
    };
    CHECK(leased.connection.async_execute({"PING"}, rejected) == 0);
    CHECK(leased.connection.async_pipeline({{"PING"}}, rejected) == 0);
    CHECK(late == 2);
    leased.connection.cancel(1);
    leased.connection.release();
    leased.connection.discard();
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool refuses its own IO worker without changing admission") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"PING"});
        remote.send("+PONG\r\n");
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    pool connections(options);
    std::promise<pool::acquire_result> ready;
    auto future = ready.get_future();
    connections.async_acquire([&](auto response) {
        CHECK_FALSE(connections.close_and_wait());
        CHECK(arknet::get_last_error() == asio::error::operation_not_supported);
        ready.set_value(std::move(response));
    });
    auto leased = await(future);
    REQUIRE(leased);
    CHECK(execute(leased.connection, {"PING"}).replies.front().text == "PONG");
    CHECK(connections.close_and_wait());
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool retires a discarded retained child without stopping a healthy replacement") {
    unsigned accepted{};
    server endpoint(
        [&](peer& remote) {
            ++accepted;
            remote.hello();
            if (accepted == 2)
            {
                remote.expect({"PING"});
                remote.send("+PONG\r\n");
            }
            remote.wait_closed();
        },
        2);
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    const auto before = tracked_transport::destroyed.load();
    tracked_pool connections(options);
    auto exited = std::make_shared<std::atomic<bool>>(false);
    std::promise<tracked_pool::acquire_result> first_acquired;
    auto ready = first_acquired.get_future();
    std::thread::id first_worker;
    connections.async_acquire([&, exited](auto response) {
        first_worker = std::this_thread::get_id();
        record_worker_exit(exited);
        first_acquired.set_value(std::move(response));
    });
    auto first = await(ready);
    REQUIRE(first);
    first.connection.discard();
    auto second = acquire(connections);
    REQUIRE(second);
    CHECK_FALSE(*exited);
    std::promise<void> same_worker;
    auto running = same_worker.get_future();
    second.connection.async_execute({"PING"}, [&](result response) {
        CHECK(response.replies.front().text == "PONG");
        CHECK(std::this_thread::get_id() == first_worker);
        same_worker.set_value();
    });
    await(running);
    CHECK(tracked_transport::destroyed == before);
    CHECK(connections.close_and_wait());
    CHECK(tracked_transport::destroyed == before + 2);
    CHECK(*exited);
    CHECK_FALSE(first.connection);
    CHECK_FALSE(second.connection);
    CHECK(first.connection.async_execute({"PING"}, {}) == 0);
    CHECK(second.connection.async_execute({"PING"}, {}) == 0);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool refuses finalizer reentry while destroying retained transport resources") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    tracked_pool connections(options);
    auto leased = acquire(connections);
    REQUIRE(leased);
    unsigned refused{};
    {
        std::lock_guard lock(tracked_transport::callbacks_mutex);
        tracked_transport::on_destroy = [&] {
            CHECK_FALSE(connections.close_and_wait());
            CHECK(arknet::get_last_error() == asio::error::operation_not_supported);
            ++refused;
        };
    }
    CHECK(connections.close_and_wait());
    CHECK(refused == 1);
    CHECK(connections.close_and_wait());
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis permanently closed leases remain inert after their pool facade is destroyed") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    tracked_pool::lease retained;
    const auto before = tracked_transport::destroyed.load();
    {
        tracked_pool connections(options);
        auto response = acquire(connections);
        REQUIRE(response);
        retained = std::move(response.connection);
        CHECK(connections.close_and_wait());
    }
    CHECK(tracked_transport::destroyed == before + 1);
    const auto caller = std::this_thread::get_id();
    unsigned completed{};
    CHECK(retained.async_execute({"PING"}, [&](result response) {
        CHECK(std::this_thread::get_id() == caller);
        CHECK(response.error == client_errc::closed);
        ++completed;
    }) == 0);
    CHECK(completed == 1);
    retained.cancel(1);
    retained.release();
    retained.discard();
    CHECK_FALSE(retained);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool shutdown retires an incomplete RESET and an old released lease") {
    std::promise<void> reset;
    auto active = reset.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"RESET"});
        reset.set_value();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto leased = acquire(connections);
    REQUIRE(leased);
    leased.connection.release();
    REQUIRE(active.wait_for(5s) == std::future_status::ready);
    CHECK(connections.close_and_wait());
    CHECK(leased.connection.async_execute({"PING"}, {}) == 0);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool closes all simultaneous children while preserving the other lease after discard") {
    paired_server endpoint;
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    options.max_connections = 2;
    tracked_pool connections(options);
    const auto before = tracked_transport::destroyed.load();
    auto first = acquire(connections);
    auto second = acquire(connections);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(execute(first.connection, {"PING"}).replies.front().text == "PONG");
    first.connection.discard();
    CHECK(execute(second.connection, {"PING"}).replies.front().text == "PONG");
    CHECK(tracked_transport::destroyed == before);
    CHECK(connections.close_and_wait());
    CHECK(tracked_transport::destroyed == before + 2);
    CHECK_FALSE(first.connection);
    CHECK_FALSE(second.connection);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool ordinary close can be followed by permanent worker retirement") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto leased = acquire(connections);
    REQUIRE(leased);
    connections.close();
    CHECK(connections.close_and_wait());
    CHECK(leased.connection.async_execute({"PING"}, {}) == 0);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool concurrent finalizers wait for a running callback and its captures to retire") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"PING"});
        remote.send("+PONG\r\n");
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    tracked_pool connections(options);
    auto leased = acquire(connections);
    REQUIRE(leased);
    std::latch entered(1), release(1), started(3);
    auto exited = std::make_shared<std::atomic<bool>>(false);
    std::atomic<unsigned> destroyed{}, completed{}, finalized{};
    auto capture = std::make_shared<capture_probe>(destroyed);
    leased.connection.async_execute({"PING"}, [&, capture, exited](result response) {
        CHECK(response);
        record_worker_exit(exited);
        entered.count_down();
        release.wait();
        ++completed;
    });
    capture.reset();
    entered.wait();
    std::vector<std::thread> finalizers;
    for (unsigned index = 0; index < 3; ++index)
    {
        finalizers.emplace_back([&] {
            started.count_down();
            if (connections.close_and_wait())
            {
                CHECK(completed == 1);
                CHECK(destroyed == 1);
                CHECK(*exited);
                ++finalized;
            }
        });
    }
    started.wait();
    CHECK(finalized == 0);
    CHECK(destroyed == 0);
    release.count_down();
    for (auto& finalizer : finalizers)
    {
        finalizer.join();
    }
    CHECK(finalized == 3);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool shutdown waits for an accepted lease publication paused before Asio post") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    tracked_pool connections(options);
    auto leased = acquire(connections);
    REQUIRE(leased);
    const auto constructed = tracked_transport::constructed.load();
    const auto retired = tracked_transport::destroyed.load();
    publication_probe probe;
    tracked_transport::probe = &probe;
    std::atomic<unsigned> destroyed{}, completed{};
    auto capture = std::make_shared<capture_probe>(destroyed);
    std::thread publisher([&, capture = std::move(capture)]() mutable {
        leased.connection.async_execute({"PING"}, [&, capture = std::move(capture)](result) { ++completed; });
    });
    probe.entered.wait();
    std::promise<bool> closed;
    auto future = closed.get_future();
    std::thread finalizer([&] { closed.set_value(connections.close_and_wait()); });
    bool gate_seen{};
    std::atomic<unsigned> acquisitions{}, acquired{};
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        ++acquisitions;
        if (connections.async_acquire([&](auto response) {
                CHECK((response.error == client_errc::closed || response.error == client_errc::queue_full));
                ++acquired;
            }) == 0)
        {
            gate_seen = true;
            break;
        }
        std::this_thread::yield();
    }
    CHECK(gate_seen);
    CHECK(future.wait_for(0ms) == std::future_status::timeout);
    CHECK(destroyed == 0);
    probe.release.count_down();
    publisher.join();
    CHECK(await(future));
    finalizer.join();
    CHECK(completed == 1);
    CHECK(destroyed == 1);
    CHECK(acquired == acquisitions);
    CHECK(tracked_transport::destroyed - retired == tracked_transport::constructed - constructed + 1);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool retires a late failed transport connect without duplicate Redis completion") {
    asio::io_context reserved_context;
    asio::ip::tcp::acceptor reserved(reserved_context, {asio::ip::address_v4::loopback(), 0});
    arknet::redis::pool_options options;
    options.connection.port = std::to_string(reserved.local_endpoint().port());
    reserved.close();
    // A closed listener gives an actual connection refusal. On Darwin, a
    // bound non-listening socket can silently drop SYNs instead of refusing.
    tracked_pool connections(options);
    const auto errors = tracked_transport::late_connect_errors.load();
    const auto retired = tracked_transport::destroyed.load();
    const auto constructed = tracked_transport::constructed.load();
    std::atomic<unsigned> completed{};
    std::promise<void> terminal;
    auto closed = terminal.get_future();

    struct reset_connect_observer
    {
        ~reset_connect_observer() {
            std::lock_guard lock(tracked_transport::callbacks_mutex);
            tracked_transport::before_connect = {};
        }
    } reset_observer;

    {
        std::lock_guard lock(tracked_transport::callbacks_mutex);
        tracked_transport::before_connect = [&](arknet::error_code error) {
            CHECK(bool(error));
            // This is the real native completion on the pool actor. Close
            // inline before forwarding its unchanged error to Redis.
            connections.close();
            CHECK(completed == 1);
            terminal.set_value();
        };
    }
    connections.async_acquire([&](auto response) {
        CHECK(response.error == client_errc::closed);
        ++completed;
    });
    await(closed);
    CHECK(connections.close_and_wait());
    CHECK(completed == 1);
    CHECK(tracked_transport::late_connect_errors == errors + 1);
    CHECK(tracked_transport::destroyed > retired);
    CHECK(tracked_transport::destroyed - retired == tracked_transport::constructed - constructed);
}

DOCTEST_TEST_CASE("Redis pool shutdown cancels a blackholed connect after a queued transport start") {
    asio::io_context reserved_context;
    asio::ip::tcp::socket reserved(reserved_context);
    reserved.open(asio::ip::tcp::v4());
    reserved.bind({asio::ip::address_v4::loopback(), 0});
    // On Darwin a bound, non-listening socket drops SYNs. Elsewhere it may
    // immediately refuse. Both paths must retire after explicit shutdown.
    arknet::redis::pool_options options;
    options.connection.port = std::to_string(reserved.local_endpoint().port());
    options.client_configuration.connect_timeout = 30s;
    tracked_pool connections(options);
    publication_probe probe;
    tracked_transport::connect_probe = &probe;
    const auto retired = tracked_transport::destroyed.load();
    std::atomic<unsigned> completed{};
    connections.async_acquire([&](auto response) {
        CHECK(response.error == client_errc::closed);
        ++completed;
    });
    probe.entered.wait();
    std::promise<bool> closed;
    auto ready = closed.get_future();
    std::thread finalizer([&] { closed.set_value(connections.close_and_wait()); });
    bool gate_seen{};
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (connections.async_acquire({}) == 0)
        {
            gate_seen = true;
            break;
        }
        std::this_thread::yield();
    }
    CHECK(gate_seen);
    probe.release.count_down();
    CHECK(await(ready));
    finalizer.join();
    CHECK(completed == 1);
    CHECK(tracked_transport::destroyed > retired);
}

DOCTEST_TEST_CASE("Redis pool shutdown rejects synchronous reentry from a lease publication") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"PING"});
        remote.send("+PONG\r\n");
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    tracked_pool connections(options);
    auto leased = acquire(connections);
    REQUIRE(leased);
    bool rejected{};
    {
        std::lock_guard lock(tracked_transport::callbacks_mutex);
        tracked_transport::reenter = [&] {
            rejected =
                !connections.close_and_wait() && arknet::get_last_error() == asio::error::operation_not_supported;
        };
    }
    CHECK(execute(leased.connection, {"PING"}));
    CHECK(rejected);
    CHECK(connections.close_and_wait());
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool gates concurrent lease and acquire publishers before its worker join") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto leased = acquire(connections);
    REQUIRE(leased);
    std::latch start(1);
    std::atomic<unsigned> completed{}, calls{};
    std::vector<std::thread> publishers;
    for (unsigned index = 0; index < 3; ++index)
    {
        publishers.emplace_back([&] {
            start.wait();
            for (unsigned attempt = 0; attempt < 64; ++attempt)
            {
                ++calls;
                leased.connection.async_execute({"PING"}, [&](result response) {
                    CHECK((response.error == client_errc::closed || response.error == client_errc::not_connected ||
                           response.error == client_errc::cancelled));
                    ++completed;
                });
                ++calls;
                const auto id = connections.async_acquire([&](auto response) {
                    CHECK((response.error == client_errc::closed || response.error == client_errc::cancelled));
                    ++completed;
                });
                connections.cancel(id);
                leased.connection.cancel(attempt + 1);
            }
        });
    }
    start.count_down();
    CHECK(connections.close_and_wait());
    for (auto& publisher : publishers)
    {
        publisher.join();
    }
    CHECK(completed == calls);
    CHECK(calls == 384);
    CHECK(connections.close_and_wait());
    endpoint.finish();
}
