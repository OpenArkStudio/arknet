// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#if defined(_MSC_VER)
#include "scoped_asio_allocation_failure.hpp"

#endif

#include <array>
#include <atomic>
#include <future>
#include <latch>
#include <memory>
#include <new>
#include <thread>
#include <utility>

#include <arknet/redis/pool.hpp>
#include "redis_test_peer.hpp"
#include "scoped_allocation_failure.hpp"

namespace {

using namespace redis_test;
using pool = arknet::redis::pool;

auto acquire(pool& connections) {
    auto completion = std::make_shared<std::promise<pool::acquire_result>>();
    auto future = completion->get_future();
    connections.async_acquire([completion](auto value) { completion->set_value(std::move(value)); });
    return await(future);
}

result execute(pool::lease& leased, command arguments) {
    auto completion = std::make_shared<std::promise<result>>();
    auto future = completion->get_future();
    leased.async_execute(std::move(arguments), [completion](auto value) { completion->set_value(std::move(value)); });
    return await(future);
}

void actor_barrier(pool& connections) {
    auto completion = std::make_shared<std::promise<client_errc>>();
    auto future = completion->get_future();
    connections.async_acquire([completion](auto value) { completion->set_value(value.error); }, -1ms);
    DOCTEST_CHECK(await(future) == client_errc::invalid_command);
}

void check_diagnostics(
    const pool& connections, std::uint64_t expected, arknet::redis::pool_diagnostics* retained = nullptr) {
    const auto info = connections.diagnostics();
    if (retained)
    {
        *retained = info;
    }
    DOCTEST_CHECK(info.return_submission_failures == expected);
    DOCTEST_REQUIRE(bool(info.last_return_submission_exception));
    DOCTEST_CHECK_THROWS_AS(std::rethrow_exception(info.last_return_submission_exception), std::bad_alloc);
}

arknet::redis::pool_options options_for(const server& endpoint) {
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    return options;
}

auto two_connections(unsigned& accepted) {
    return [&accepted](peer& remote) {
        ++accepted;
        remote.hello();
        remote.expect({"PING"});
        remote.send(accepted == 1 ? "+FIRST\r\n" : "+SECOND\r\n");
        remote.wait_closed();
    };
}

template<class Submit>
void arm_on_actor_before(pool& connections, Submit submit) {
    auto entered = std::make_shared<std::promise<void>>();
    auto ready = entered->get_future();
    auto posted = std::make_shared<std::latch>(1);
    connections.async_acquire(
        [entered, posted](auto value) {
            DOCTEST_CHECK(value.error == client_errc::invalid_command);
            entered->set_value();
            posted->wait();
            scoped_allocation_failure::arm();
        },
        -1ms);
    if (ready.wait_for(5s) != std::future_status::ready)
    {
        posted->count_down();
        DOCTEST_FAIL("pool actor did not reach allocation arm barrier");
    }
    try
    { submit(); }
    catch (...)
    {
        posted->count_down();
        throw;
    }
    posted->count_down();
}

} // namespace

DOCTEST_TEST_CASE("Redis pool return failure from external lease destruction "
                  "preserves results and capacity") {
    unsigned accepted{};
    server endpoint(two_connections(accepted), 2);
    auto options = options_for(endpoint);
    options.max_waiters = 1;
    pool connections(options);
    const auto empty = connections.diagnostics();
    DOCTEST_CHECK(empty.return_submission_failures == 0);
    DOCTEST_CHECK_FALSE(bool(empty.last_return_submission_exception));
    auto held = std::make_unique<pool::acquire_result>(acquire(connections));
    DOCTEST_REQUIRE(bool(*held));
    auto reply = execute(held->connection, {"PING"});
    DOCTEST_REQUIRE(bool(reply));
    DOCTEST_CHECK(reply.replies.front().text == "FIRST");
    auto waiting = std::make_shared<std::promise<pool::acquire_result>>();
    auto waited = waiting->get_future();
    connections.async_acquire([waiting](auto response) { waiting->set_value(std::move(response)); });
    actor_barrier(connections);
    const auto before = scoped_allocation_failure::injections.load();
    {
        scoped_allocation_failure::scope failure;
        held.reset();
    }
    DOCTEST_CHECK(scoped_allocation_failure::injections == before + 1);
    check_diagnostics(connections, 1);
    // This rejected acquisition still drives recovery and pumps the older
    // admitted waiter without a second close attempt in the same actor pass.
    auto rejected = acquire(connections);
    DOCTEST_CHECK(rejected.error == client_errc::queue_full);
    auto fresh = await(waited);
    DOCTEST_REQUIRE(bool(fresh));
    DOCTEST_CHECK(execute(fresh.connection, {"PING"}).replies.front().text == "SECOND");
    check_diagnostics(connections, 1);
    connections.close();
    endpoint.finish();
    DOCTEST_CHECK(accepted == 2);
}

DOCTEST_TEST_CASE("Redis pool post-handler finish failure preserves its "
                  "successful response") {
    unsigned accepted{};
    server endpoint(two_connections(accepted), 2);
    pool connections(options_for(endpoint));
    auto held = acquire(connections);
    DOCTEST_REQUIRE(bool(held));
    auto completion = std::make_shared<std::promise<result>>();
    auto future = completion->get_future();
    const auto before = scoped_allocation_failure::injections.load();
    held.connection.async_execute({"PING"}, [&](result reply) {
        held.connection.release();
        completion->set_value(std::move(reply));
        // No test/framework work after arming: native finish owns the next
        // allocation.
        scoped_allocation_failure::arm();
    });
    auto reply = await(future);
    DOCTEST_REQUIRE(bool(reply));
    DOCTEST_CHECK(reply.replies.front().text == "FIRST");
    actor_barrier(connections);
    DOCTEST_CHECK(scoped_allocation_failure::injections == before + 1);
    check_diagnostics(connections, 1);
    auto fresh = acquire(connections);
    DOCTEST_REQUIRE(bool(fresh));
    DOCTEST_CHECK(execute(fresh.connection, {"PING"}).replies.front().text == "SECOND");
    connections.close();
    endpoint.finish();
    DOCTEST_CHECK(accepted == 2);
}

DOCTEST_TEST_CASE("Redis pool retains repeated close-publication failures "
                  "until a later actor pass") {
    unsigned accepted{};
    server endpoint(two_connections(accepted), 2);
    pool connections(options_for(endpoint));
    std::atomic_bool coherent{true};
    std::latch reader_started(1);
    // Keep each inspected exception alive through the concurrent reader's
    // join. Count, coherence and exception-type checks still run concurrently;
    // only final exception-object release is ordered after the reader exits.
    std::array<arknet::redis::pool_diagnostics, 5> retained;
    std::jthread observer([&](std::stop_token stop) {
        std::uint64_t previous{};
        reader_started.count_down();
        while (!stop.stop_requested())
        {
            const auto info = connections.diagnostics();
            if (info.return_submission_failures < previous ||
                bool(info.last_return_submission_exception) != (info.return_submission_failures != 0))
            {
                coherent = false;
            }
            previous = info.return_submission_failures;
            std::this_thread::yield();
        }
    });
    reader_started.wait();
    auto held = std::make_unique<pool::acquire_result>(acquire(connections));
    DOCTEST_REQUIRE(bool(*held));
    DOCTEST_CHECK(execute(held->connection, {"PING"}).replies.front().text == "FIRST");
    actor_barrier(connections);
    const auto before = scoped_allocation_failure::injections.load();
    {
        scoped_allocation_failure::scope failure;
        held.reset();
    }
    DOCTEST_CHECK(scoped_allocation_failure::injections == before + 1);
    check_diagnostics(connections, 1, &retained[0]);
    auto completion = std::make_shared<std::promise<pool::acquire_result>>();
    auto future = completion->get_future();
    arm_on_actor_before(connections, [&] {
        connections.async_acquire([completion](auto response) { completion->set_value(std::move(response)); });
    });
    actor_barrier(connections);
    DOCTEST_CHECK(scoped_allocation_failure::injections == before + 2);
    check_diagnostics(connections, 2, &retained[1]);
    DOCTEST_CHECK(future.wait_for(0ms) != std::future_status::ready);
    for (unsigned attempt = 0; attempt != 2; ++attempt)
    {
        arm_on_actor_before(connections, [&] { connections.cancel(0); });
        actor_barrier(connections);
        DOCTEST_CHECK(scoped_allocation_failure::injections == before + 3 + attempt);
        check_diagnostics(connections, 3 + attempt, &retained[2 + attempt]);
        DOCTEST_CHECK(future.wait_for(0ms) != std::future_status::ready);
    }
    DOCTEST_CHECK(scoped_allocation_failure::injections == before + 4);
    connections.cancel(0);
    auto fresh = await(future);
    DOCTEST_REQUIRE(bool(fresh));
    DOCTEST_CHECK(execute(fresh.connection, {"PING"}).replies.front().text == "SECOND");
    check_diagnostics(connections, 4, &retained[4]);
    observer.request_stop();
    observer.join();
    DOCTEST_CHECK(coherent);
    connections.close();
    endpoint.finish();
    DOCTEST_CHECK(accepted == 2);
}

DOCTEST_TEST_CASE("Redis pool failed return recovery preserves the original acquire deadline") {
    unsigned accepted{};
    server endpoint(two_connections(accepted), 2);
    pool connections(options_for(endpoint));
    auto held = std::make_unique<pool::acquire_result>(acquire(connections));
    DOCTEST_REQUIRE(bool(*held));
    DOCTEST_CHECK(execute(held->connection, {"PING"}).replies.front().text == "FIRST");
    auto completion = std::make_shared<std::promise<pool::acquire_result>>();
    auto future = completion->get_future();
    connections.async_acquire([completion](auto response) { completion->set_value(std::move(response)); }, 300ms);
    actor_barrier(connections);
    DOCTEST_CHECK(future.wait_for(0ms) != std::future_status::ready);
    const auto before = scoped_allocation_failure::injections.load();
    {
        scoped_allocation_failure::scope failure;
        held.reset();
    }
    DOCTEST_CHECK(scoped_allocation_failure::injections == before + 1);
    arm_on_actor_before(connections, [&] { connections.cancel(0); });
    // This bound detects a lost/replaced timer; it is not a latency assertion.
    DOCTEST_REQUIRE(future.wait_for(2s) == std::future_status::ready);
    DOCTEST_CHECK(future.get().error == client_errc::timeout);
    check_diagnostics(connections, 2);
    DOCTEST_CHECK(scoped_allocation_failure::injections == before + 2);
    auto fresh = acquire(connections);
    DOCTEST_REQUIRE(bool(fresh));
    DOCTEST_CHECK(execute(fresh.connection, {"PING"}).replies.front().text == "SECOND");
    connections.close();
    endpoint.finish();
    DOCTEST_CHECK(accepted == 2);
}
