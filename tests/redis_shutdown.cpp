// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "redis_test_peer.hpp"

namespace redis_shutdown_test {

struct tracked_transport : arknet::tcp_client
{
    using arknet::tcp_client::tcp_client;
    inline static std::atomic<unsigned> destroyed{};

    ~tracked_transport() {
        ++destroyed;
    }
};

// A distinct transport lets the test exercise the existing pool-friend
// constructor without exposing a raw child client in the production pool API.
struct borrowed_transport : arknet::tcp_client
{
    using arknet::tcp_client::tcp_client;
};

struct publication_probe
{
    std::latch entered{1};
    std::latch release{1};
};

struct publication_transport : arknet::tcp_client
{
    using arknet::tcp_client::tcp_client;
    inline static std::atomic<publication_probe*> probe{};
    inline static std::function<void()> reenter;

    auto& io() {
        if (auto* current = probe.exchange(nullptr))
        {
            current->entered.count_down();
            current->release.wait();
        }
        if (auto callback = std::exchange(reenter, {}))
        {
            callback();
        }
        return arknet::tcp_client::io();
    }
};

} // namespace redis_shutdown_test

namespace arknet::redis {

template<>
class basic_pool<redis_shutdown_test::borrowed_transport>
{
public:
    using borrowed_client = basic_client<redis_shutdown_test::borrowed_transport>;

    static std::unique_ptr<borrowed_client> make(std::shared_ptr<arknet::detail::io_pool> runtime) {
        return std::unique_ptr<borrowed_client>(new borrowed_client({}, {}, std::move(runtime)));
    }

    static void reset(
        borrowed_client& client, basic_client<redis_shutdown_test::borrowed_transport>::completion_handler f) {
        client.async_reset(std::move(f), std::chrono::seconds(1));
    }
};

} // namespace arknet::redis

namespace {

using namespace redis_test;
using namespace std::chrono_literals;
using tracked_client = arknet::redis::basic_client<redis_shutdown_test::tracked_transport>;

struct destruction_probe
{
    std::atomic<unsigned>& destroyed;

    ~destruction_probe() {
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

struct reenter_probe
{
    client* owner{};
    std::atomic<unsigned>* rejected{};

    ~reenter_probe() {
        if (!owner->close_and_wait() && arknet::get_last_error() == asio::error::operation_not_supported)
        {
            ++*rejected;
        }
    }
};

} // namespace

DOCTEST_TEST_CASE("Redis permanent shutdown before connect joins its worker and synchronously rejects new APIs") {
    const auto before = redis_shutdown_test::tracked_transport::destroyed.load();
    tracked_client connection;
    CHECK(connection.close_and_wait());
    CHECK_FALSE(connection.is_connected());
    CHECK(redis_shutdown_test::tracked_transport::destroyed == before + 1);
    CHECK(connection.close_and_wait());
    const auto caller = std::this_thread::get_id();
    unsigned completions{};
    auto rejected = [&](result response) {
        CHECK(std::this_thread::get_id() == caller);
        CHECK(response.error == client_errc::closed);
        CHECK_FALSE(response.command_may_have_run);
        ++completions;
    };
    connection.async_connect({}, rejected);
    CHECK(connection.async_execute({"PING"}, rejected) == 0);
    CHECK(connection.async_pipeline({{"PING"}, {"ECHO", "a"}}, rejected) == 0);
    CHECK(completions == 3);
    std::atomic<unsigned> captures{};
    auto capture = std::make_shared<destruction_probe>(captures);
    connection.set_push_handler([capture](auto) { FAIL("Terminated client must not publish a push handler"); });
    capture.reset();
    CHECK(captures == 1);
    connection.cancel(1);
    connection.close();
    CHECK(connection.close_and_wait());
    CHECK_THROWS_AS(connection.async_execute({"PING"}, [](auto) { throw std::runtime_error("caller rejection"); }),
        std::runtime_error);
    CHECK(connection.close_and_wait());
}

DOCTEST_TEST_CASE("Redis permanent shutdown retires handshake handlers captures and the owned worker") {
    std::promise<void> received;
    auto ready = received.get_future();
    server endpoint([&](peer& remote) {
        remote.expect({"HELLO", "3"});
        received.set_value();
        remote.wait_closed();
    });
    tracked_client connection;
    const auto before = redis_shutdown_test::tracked_transport::destroyed.load();
    std::atomic<unsigned> destroyed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    auto exited = std::make_shared<std::atomic<bool>>(false);
    unsigned completions{};
    result response;
    arknet::redis::connect_options options;
    options.port = endpoint.port();
    connection.async_connect(options, [&, capture, exited](result reply) {
        record_worker_exit(exited);
        response = std::move(reply);
        ++completions;
    });
    capture.reset();
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    CHECK(connection.close_and_wait());
    CHECK(response.error == client_errc::closed);
    CHECK(completions == 1);
    CHECK(destroyed == 1);
    CHECK(exited->load());
    CHECK(redis_shutdown_test::tracked_transport::destroyed == before + 1);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis permanent shutdown retires active pipeline and cancelled tombstone captures") {
    std::promise<void> received;
    auto ready = received.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"ECHO", "active"});
        remote.expect({"PING"});
        received.set_value();
        remote.wait_closed();
    });
    client connection;
    REQUIRE(bool(connect(connection, endpoint.port())));
    std::atomic<unsigned> destroyed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    auto exited = std::make_shared<std::atomic<bool>>(false);
    result response;
    unsigned completions{};
    const auto id = connection.async_pipeline({{"ECHO", "active"}, {"PING"}}, [&, capture, exited](result reply) {
        record_worker_exit(exited);
        response = std::move(reply);
        ++completions;
    });
    capture.reset();
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    connection.cancel(id);
    CHECK(connection.close_and_wait());
    CHECK(response.error == client_errc::cancelled);
    CHECK(response.command_may_have_run);
    CHECK(completions == 1);
    CHECK(destroyed == 1);
    CHECK(exited->load());
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis shutdown joins the entire accepted command callback before reporting success") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"PING"});
        remote.send("+PONG\r\n");
        remote.wait_closed();
    });
    client connection;
    REQUIRE(bool(connect(connection, endpoint.port())));
    std::promise<void> entered;
    auto inside = entered.get_future();
    std::latch release(1);
    std::atomic<bool> returned{};
    auto exited = std::make_shared<std::atomic<bool>>(false);
    connection.async_execute({"PING"}, [&](result response) {
        CHECK(bool(response));
        record_worker_exit(exited);
        entered.set_value();
        release.wait();
        returned = true;
    });
    REQUIRE(inside.wait_for(5s) == std::future_status::ready);
    std::promise<bool> closed;
    auto future = closed.get_future();
    std::thread waiter([&] { closed.set_value(connection.close_and_wait()); });
    CHECK(future.wait_for(30ms) == std::future_status::timeout);
    release.count_down();
    CHECK(await(future));
    waiter.join();
    CHECK(returned.load());
    CHECK(exited->load());
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis shutdown can finish after its accepted callback destroys the facade") {
    auto connection = std::make_unique<tracked_client>();
    auto* facade = connection.get();
    const auto before = redis_shutdown_test::tracked_transport::destroyed.load();
    std::promise<void> entered;
    auto inside = entered.get_future();
    std::latch release(1);
    connection->async_execute({"PING"}, [&](result) {
        entered.set_value();
        release.wait();
        connection.reset();
    });
    REQUIRE(inside.wait_for(5s) == std::future_status::ready);
    std::promise<bool> closed;
    auto future = closed.get_future();
    std::thread finalizer([&] { closed.set_value(facade->close_and_wait()); });
    bool gate_seen{};
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (facade->async_execute({"PING"}, {}) == 0)
        {
            gate_seen = true;
            break;
        }
        std::this_thread::yield();
    }
    CHECK(gate_seen);
    release.count_down();
    CHECK(await(future));
    finalizer.join();
    CHECK_FALSE(connection);
    CHECK(redis_shutdown_test::tracked_transport::destroyed == before + 1);
}

DOCTEST_TEST_CASE("Redis own IO lane rejects permanent shutdown without changing the ordinary client") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"PING"});
        remote.send("+PONG\r\n");
        remote.wait_closed();
    });
    client connection;
    std::promise<bool> checked;
    auto future = checked.get_future();
    arknet::redis::connect_options options;
    options.port = endpoint.port();
    connection.async_connect(options, [&](result response) {
        CHECK(bool(response));
        checked.set_value(
            !connection.close_and_wait() && arknet::get_last_error() == asio::error::operation_not_supported);
    });
    CHECK(await(future));
    std::promise<result> replied;
    auto reply = replied.get_future();
    connection.async_execute({"PING"}, [&](result response) { replied.set_value(std::move(response)); });
    CHECK(bool(await(reply)));
    CHECK(connection.close_and_wait());
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis ordinary close stays terminal and a later permanent shutdown still drains") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.wait_closed();
    });
    client connection;
    REQUIRE(bool(connect(connection, endpoint.port())));
    connection.close();
    std::promise<result> rejected;
    auto future = rejected.get_future();
    connection.async_connect({}, [&](result response) { rejected.set_value(std::move(response)); });
    CHECK(await(future).error == client_errc::closed);
    CHECK(connection.close_and_wait());
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis permanent shutdown handles capture-destructor reentry without self waiting") {
    client connection;
    std::atomic<unsigned> rejected{};
    auto capture = std::make_shared<reenter_probe>();
    capture->owner = &connection;
    capture->rejected = &rejected;
    connection.set_push_handler([capture](auto) {});
    capture.reset();
    CHECK(connection.close_and_wait());
    CHECK(rejected == 1);
    CHECK(connection.close_and_wait());
}

DOCTEST_TEST_CASE("Redis simultaneous permanent shutdown callers observe completed resource retirement") {
    client connection;
    std::atomic<unsigned> destroyed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    std::atomic<unsigned> completions{};
    connection.async_execute({"not connected"}, [&, capture](result) { ++completions; });
    capture.reset();
    std::latch start(1);
    std::atomic<unsigned> closed{};
    std::vector<std::thread> waiters;
    for (unsigned index = 0; index < 3; ++index)
    {
        waiters.emplace_back([&] {
            start.wait();
            if (connection.close_and_wait())
            {
                CHECK(destroyed == 1);
                CHECK(completions == 1);
                ++closed;
            }
        });
    }
    start.count_down();
    for (auto& waiter : waiters)
    {
        waiter.join();
    }
    CHECK(closed == 3);
}

DOCTEST_TEST_CASE("Redis permanent shutdown cannot stop a pool-borrowed parent runtime") {
    using pool_access = arknet::redis::basic_pool<redis_shutdown_test::borrowed_transport>;
    auto runtime = std::make_shared<arknet::detail::io_pool>(1);
    auto connection = pool_access::make(runtime);
    CHECK_FALSE(connection->close_and_wait());
    CHECK(arknet::get_last_error() == asio::error::operation_not_supported);
    CHECK(runtime->started());
    std::promise<bool> reset;
    auto reset_future = reset.get_future();
    pool_access::reset(*connection, [&](result response) { reset.set_value(response.error == client_errc::closed); });
    CHECK(await(reset_future));
    connection.reset();
    std::promise<void> running;
    auto future = running.get_future();
    asio::post(runtime->get()->executor(), [&] { running.set_value(); });
    await(future);
    CHECK(runtime->started());
    runtime->stop();

    pool_access::borrowed_client independent;
    CHECK(independent.close_and_wait());
    const auto caller = std::this_thread::get_id();
    bool reset_rejected{};
    pool_access::reset(independent, [&](result response) {
        CHECK(std::this_thread::get_id() == caller);
        reset_rejected = response.error == client_errc::closed;
    });
    CHECK(reset_rejected);
}

DOCTEST_TEST_CASE("Redis permanent shutdown fences an accepted publisher paused before Asio post") {
    using transport = redis_shutdown_test::publication_transport;
    arknet::redis::basic_client<transport> connection;
    redis_shutdown_test::publication_probe probe;
    transport::probe = &probe;
    std::atomic<unsigned> destroyed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    std::atomic<unsigned> completions{};
    std::thread publisher([&, capture = std::move(capture)]() mutable {
        connection.async_execute({"PING"}, [&, capture = std::move(capture)](result) { ++completions; });
    });
    capture.reset();
    probe.entered.wait();
    std::promise<bool> closed;
    auto future = closed.get_future();
    std::thread finalizer([&] { closed.set_value(connection.close_and_wait()); });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    bool gate_seen{};
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (connection.async_execute({"PING"}, {}) == 0)
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
    CHECK(await(future));
    CHECK(destroyed == 1);
    publisher.join();
    finalizer.join();
    CHECK(completions == 1);
    CHECK(destroyed == 1);
}

DOCTEST_TEST_CASE("Redis permanent shutdown rejects synchronous reentry from an active API publication") {
    using transport = redis_shutdown_test::publication_transport;
    arknet::redis::basic_client<transport> connection;
    bool rejected{};
    transport::reenter = [&] {
        rejected = !connection.close_and_wait() && arknet::get_last_error() == asio::error::operation_not_supported;
    };
    std::promise<result> complete;
    auto future = complete.get_future();
    connection.async_execute({"PING"}, [&](result response) { complete.set_value(std::move(response)); });
    CHECK(rejected);
    CHECK(await(future).error == client_errc::not_connected);
    CHECK(connection.close_and_wait());
}

DOCTEST_TEST_CASE("Redis concurrent API callers are fenced before the permanent shutdown worker join") {
    client connection;
    std::latch start(1);
    std::atomic<unsigned> calls{};
    std::atomic<unsigned> completions{};
    std::vector<std::thread> publishers;
    for (unsigned lane = 0; lane < 4; ++lane)
    {
        publishers.emplace_back([&] {
            start.wait();
            for (unsigned index = 0; index < 64; ++index)
            {
                ++calls;
                connection.async_execute({"PING"}, [&](result response) {
                    CHECK((response.error == client_errc::not_connected || response.error == client_errc::closed));
                    ++completions;
                });
                connection.set_push_handler([](auto) {});
                connection.cancel(index + 1);
            }
        });
    }
    start.count_down();
    CHECK(connection.close_and_wait());
    for (auto& publisher : publishers)
    {
        publisher.join();
    }
    CHECK(completions == calls);
    CHECK(calls == 256);
    CHECK(connection.close_and_wait());
}
