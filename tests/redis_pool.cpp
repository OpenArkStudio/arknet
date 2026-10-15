// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <atomic>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <utility>

#include <arknet/redis/pool.hpp>
#include "check.hpp"
#include "redis_test_peer.hpp"

namespace {

using namespace redis_test;
using arknet::redis::pool;

template<class Pool>
auto acquire(Pool& connections) {
    std::promise<typename Pool::acquire_result> completed;
    auto future = completed.get_future();
    connections.async_acquire([&](auto response) { completed.set_value(std::move(response)); });
    return await(future);
}

template<class Connection>
result execute(Connection& connection, command arguments) {
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_execute(std::move(arguments), [&](result reply) { completed.set_value(std::move(reply)); });
    return await(future);
}

template<class Connection>
result pipeline(Connection& connection, std::vector<command> commands) {
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_pipeline(std::move(commands), [&](result reply) { completed.set_value(std::move(reply)); });
    return await(future);
}

} // namespace

DOCTEST_TEST_CASE("Redis pool reuses a connection only after RESET and a complete new handshake") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"MULTI"});
        remote.send("+OK\r\n");
        remote.expect({"RESET"});
        remote.send("+RESET\r\n");
        remote.hello();
        remote.expect({"PING"});
        remote.send("+PONG\r\n");
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto first = acquire(connections);
    check(first, "first lease");
    check(execute(first.connection, {"MULTI"}).replies[0].text == "OK", "transaction begins");
    first.connection.release();
    std::promise<result> late;
    auto late_future = late.get_future();
    first.connection.async_execute(
        {"SET", "must-not-send", "value"}, [&](result reply) { late.set_value(std::move(reply)); });
    check(await(late_future).error == client_errc::closed, "released lease cannot access another borrower's socket");
    auto second = acquire(connections);
    check(second, "second lease after reset");
    check(execute(second.connection, {"PING"}).replies[0].text == "PONG", "same connection is ready");
    connections.close();
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis lease release waits for callbacks and bounded waiters support cancellation and deadlines") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.max_waiters = 1;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto held = acquire(connections);
    check(held, "held lease");
    std::promise<pool::acquire_result> waiting, overflow, expired;
    auto waiting_future = waiting.get_future(), overflow_future = overflow.get_future(),
         expired_future = expired.get_future();
    auto id = connections.async_acquire([&](auto response) { waiting.set_value(std::move(response)); });
    connections.async_acquire([&](auto response) { overflow.set_value(std::move(response)); });
    check(await(overflow_future).error == client_errc::queue_full,
        "waiting queue is bounded independently of leased connections");
    connections.cancel(id);
    check(await(waiting_future).error == client_errc::cancelled, "queued acquisition cancelled");
    connections.async_acquire([&](auto response) { expired.set_value(std::move(response)); }, 20ms);
    check(await(expired_future).error == client_errc::timeout, "queued acquisition deadline");
    connections.close();
    std::promise<result> closed;
    auto closed_future = closed.get_future();
    held.connection.async_execute({"PING"}, [&](result reply) { closed.set_value(std::move(reply)); });
    check(await(closed_future).error == client_errc::closed, "pool close aborts an active lease");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis lease return drains pending responses before RESET and next acquisition") {
    std::promise<void> send;
    auto allowed = send.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"GET", "slow"});
        if (allowed.wait_for(5s) != std::future_status::ready)
        {
            throw std::runtime_error("Missing response signal");
        }
        remote.send(":7\r\n");
        remote.expect({"RESET"});
        remote.send("+RESET\r\n");
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto first = acquire(connections);
    check(first, "first lease");
    std::promise<result> reply;
    auto reply_future = reply.get_future();
    first.connection.async_execute({"GET", "slow"}, [&](result response) { reply.set_value(std::move(response)); });
    first.connection.release();
    std::promise<pool::acquire_result> waiting;
    auto future = waiting.get_future();
    connections.async_acquire([&](auto response) { waiting.set_value(std::move(response)); });
    check(future.wait_for(20ms) == std::future_status::timeout, "lease cannot be reused while its operation is active");
    send.set_value();
    check(await(reply_future).replies[0].integer == 7, "original request retains its response");
    auto next = await(future);
    check(next, "next lease acquired after original completion and reset");
    connections.close();
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool RESET remains behind timed-out response tombstones") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"INCR", "slow"});
        remote.expect({"RESET"});
        remote.send(":7\r\n+RESET\r\n");
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto first = acquire(connections);
    check(first, "first lease");
    std::promise<result> reply;
    auto reply_future = reply.get_future();
    first.connection.async_execute(
        {"INCR", "slow"}, [&](result response) { reply.set_value(std::move(response)); }, 20ms);
    check(await(reply_future).error == client_errc::timeout, "sent operation timed out");
    first.connection.release();
    auto next = acquire(connections);
    check(next, "old INCR reply did not consume RESET or HELLO response slots");
    connections.close();
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool can cancel connection establishment and closes pending acquisitions once") {
    std::promise<void> hello;
    auto observed = hello.get_future();
    server endpoint([&](peer& remote) {
        remote.expect({"HELLO", "3"});
        hello.set_value();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    pool connections(options);
    std::promise<pool::acquire_result> connecting;
    auto future = connecting.get_future();
    std::atomic<unsigned> calls{};
    auto id = connections.async_acquire([&](auto response) {
        ++calls;
        connecting.set_value(std::move(response));
    });
    await(observed);
    connections.cancel(id);
    check(await(future).error == client_errc::cancelled, "connecting acquisition cancelled");
    connections.close();
    endpoint.finish();
    check(calls == 1, "late connect completion cannot complete acquisition again");
}

DOCTEST_TEST_CASE("Redis pool destruction inside an acquisition callback safely closes its lease") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.connection.port = endpoint.port();
    auto connections = std::make_unique<pool>(options);
    std::promise<void> closed;
    auto future = closed.get_future();
    connections->async_acquire([&](auto response) {
        connections.reset();
        response.connection.release();
        closed.set_value();
    });
    await(future);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool discards unsupported RESET connections before admitting another lease") {
    server endpoint(
        [round = 0](peer& remote) mutable {
            remote.hello();
            if (round++ == 0)
            {
                remote.expect({"RESET"});
                remote.send("-ERR unknown command RESET\r\n");
                remote.wait_closed();
            }
            else
            {
                remote.expect({"PING"});
                remote.send("+PONG\r\n");
                remote.wait_closed();
            }
        },
        2);
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto first = acquire(connections);
    check(first, "first connection");
    first.connection.release();
    auto second = acquire(connections);
    check(second, "replacement connection");
    check(execute(second.connection, {"PING"}).replies[0].text == "PONG",
        "unsupported reset did not leak old session state");
    connections.close();
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis pool reset deadline includes waiting for undrained sent-command replies") {
    server endpoint(
        [round = 0](peer& remote) mutable {
            remote.hello();
            if (round++ == 0)
            {
                remote.expect({"INCR", "slow"});
                remote.expect({"RESET"});
                remote.wait_closed();
            }
            else
            {
                remote.expect({"PING"});
                remote.send("+PONG\r\n");
                remote.wait_closed();
            }
        },
        2);
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.reset_timeout = 30ms;
    options.connection.port = endpoint.port();
    pool connections(options);
    auto first = acquire(connections);
    check(first, "first connection");
    std::promise<result> completed;
    auto future = completed.get_future();
    first.connection.async_execute(
        {"INCR", "slow"}, [&](result reply) { completed.set_value(std::move(reply)); }, 20ms);
    check(await(future).error == client_errc::timeout, "sent write times out without a fabricated outcome");
    first.connection.release();
    auto second = acquire(connections);
    check(second, "new connection after bounded reset deadline");
    check(execute(second.connection, {"PING"}).replies[0].text == "PONG", "uncertain write was not replayed");
    connections.close();
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis real pool isolates MULTI WATCH SELECT and AUTH across exclusive leases" *
                  doctest::skip(std::getenv("ARKNET_REDIS_TEST_PORT") == nullptr)) {
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = std::getenv("ARKNET_REDIS_TEST_PORT");
    options.connection.password = "arknet-tests-only";
    options.connection.client_name = "arknet-native-pool-test";
    pool connections(options);
    auto first = acquire(connections);
    check(first, "authenticated first lease");
    auto setup = pipeline(first.connection,
        {{"ACL", "SETUSER", "arknet-pool-isolation", "on", ">pool-test-password", "~*", "+@all"},
            {"AUTH", "arknet-pool-isolation", "pool-test-password"}, {"CLIENT", "ID"}, {"WATCH", "arknet-pool-watch"},
            {"SELECT", "3"}, {"MULTI"}, {"SET", "arknet-pool-uncommitted", "must-not-commit"}});
    check(setup && setup.replies.size() == 7 && setup.replies.back().text == "QUEUED",
        "changed user/database with open transaction");
    const auto connection_id = setup.replies[2].integer;
    first.connection.release();
    auto second = acquire(connections);
    check(second, "reused clean authenticated lease");
    auto clean = pipeline(second.connection,
        {{"CLIENT", "ID"}, {"CLIENT", "GETNAME"}, {"ACL", "WHOAMI"}, {"GET", "arknet-pool-uncommitted"}, {"EXEC"},
            {"SELECT", "3"}, {"GET", "arknet-pool-uncommitted"}, {"SELECT", "0"}});
    check(clean && clean.replies.size() == 8, "post-reset inspection");
    check(clean.replies[0].integer == connection_id, "connection was reused rather than replaced");
    check(clean.replies[1].text == options.connection.client_name && clean.replies[2].text == "default",
        "name and authentication restored");
    check(clean.replies[3].type == value_type::null && clean.replies[4].type == value_type::error &&
              clean.replies[6].type == value_type::null,
        "open transaction discarded without executing its write");
    check(execute(second.connection, {"WATCH", "arknet-pool-watch"}), "watch a key before return");
    second.connection.release();
    auto third = acquire(connections);
    check(third, "third isolated lease");
    client independent;
    check(connect(independent, options.connection.port, options.connection), "independent authenticated writer");
    check(execute(independent, {"INCR", "arknet-pool-watch"}), "change previously watched key");
    auto transaction = pipeline(third.connection, {{"MULTI"}, {"PING"}, {"EXEC"}});
    check(transaction && transaction.replies.back().type == value_type::array &&
              transaction.replies.back().elements[0].text == "PONG",
        "WATCH state did not cross lease boundaries");
    check(execute(third.connection, {"DEL", "arknet-pool-watch"}), "cleanup watched key");
    check(execute(third.connection, {"ACL", "DELUSER", "arknet-pool-isolation"}), "cleanup test user");
    connections.close();
}

#if defined(ARKNET_ENABLE_SSL)
DOCTEST_TEST_CASE("Redis TLS pool reuses only authenticated reset connections" *
                  doctest::skip(std::getenv("ARKNET_REDIS_TEST_TLS_PORT") == nullptr ||
                                std::getenv("ARKNET_REDIS_TEST_CA") == nullptr)) {
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = std::getenv("ARKNET_REDIS_TEST_TLS_PORT");
    options.connection.password = "arknet-tests-only";
    options.certificate_authority = std::getenv("ARKNET_REDIS_TEST_CA");
    arknet::redis::tls_pool connections(options);
    auto first = acquire(connections);
    check(first, "trusted TLS lease");
    auto id = execute(first.connection, {"CLIENT", "ID"});
    check(id && id.replies.size() == 1, "first server connection ID");
    first.connection.release();
    auto second = acquire(connections);
    check(second, "authenticated TLS lease after RESET");
    auto reused = execute(second.connection, {"CLIENT", "ID"});
    check(reused && reused.replies[0].integer == id.replies[0].integer,
        "TLS session reused after authentication restoration");
    connections.close();
}
#endif
