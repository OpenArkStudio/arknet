// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arknet/redis/client.hpp>
#include "check.hpp"
#include "redis_test_peer.hpp"

using namespace redis_test;

DOCTEST_TEST_CASE("Redis RESP3 handshake and fragmented binary pipeline keep pushes separate") {
    const std::string binary("a\0\r\nb", 5);
    server endpoint([&](peer& remote) {
        remote.expect({"HELLO", "3", "AUTH", "user", "password", "SETNAME", "arknet-test"});
        remote.expect({"SELECT", "2"});
        remote.send("%1\r\n+proto\r\n:3\r\n+OK\r\n");
        remote.expect({"ECHO", binary});
        remote.expect({"GET", "wrong-type"});
        remote.expect({"PING"});
        const std::string replies =
            ">2\r\n+invalidate\r\n*1\r\n+k\r\n$5\r\n" + binary + "\r\n-WRONGTYPE expected\r\n+PONG\r\n";
        for (char byte : replies)
        {
            remote.send(std::string_view(&byte, 1));
        }
    });
    client connection;
    std::promise<arknet::redis::value> pushed;
    auto push = pushed.get_future();
    connection.set_push_handler([&](auto value) { pushed.set_value(std::move(value)); });
    arknet::redis::connect_options options;
    options.username = "user";
    options.password = "password";
    options.client_name = "arknet-test";
    options.database = 2;
    check(connect(connection, endpoint.port(), options), "RESP3 auth and database handshake");
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_pipeline({{"ECHO", binary}, {"GET", "wrong-type"}, {"PING"}},
        [&](result reply) { completed.set_value(std::move(reply)); });
    auto response = await(future);
    check(response && response.replies.size() == 3, "all pipeline slots complete together");
    check(response.replies[0].type == value_type::bulk_string && response.replies[0].text == binary, "binary response");
    check(response.replies[1].type == value_type::error && response.replies[1].text == "WRONGTYPE expected",
        "server errors are ordered replies");
    check(response.replies[2].text == "PONG", "third response keeps its slot");
    auto notification = await(push);
    check(notification.type == value_type::push && notification.elements[0].text == "invalidate",
        "separate push callback");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis RESP2 performs explicit authentication without silently changing protocols") {
    server endpoint([](peer& remote) {
        remote.expect({"AUTH", "password"});
        remote.expect({"CLIENT", "SETNAME", "legacy"});
        remote.expect({"PING"});
        remote.send("+OK\r\n+OK\r\n+PONG\r\n");
        remote.expect({"PING"});
        remote.send("+PONG\r\n");
    });
    client connection;
    arknet::redis::connect_options options;
    options.protocol = arknet::redis::protocol_version::resp2;
    options.password = "password";
    options.client_name = "legacy";
    check(connect(connection, endpoint.port(), options), "RESP2 handshake");
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_execute({"PING"}, [&](result reply) { completed.set_value(std::move(reply)); });
    auto response = await(future);
    check(response && response.replies[0].text == "PONG", "RESP2 command");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis cancelled sent commands drain their own slots and retain queue budget") {
    std::promise<void> received;
    auto seen = received.get_future();
    std::promise<void> release;
    auto allowed = release.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"INCR", "first"});
        remote.expect({"INCR", "second"});
        received.set_value();
        if (allowed.wait_for(5s) != std::future_status::ready)
        {
            throw std::runtime_error("Missing release signal");
        }
        remote.send(":11\r\n:22\r\n");
        remote.expect({"INCR", "third"});
        remote.send(":33\r\n");
    });
    arknet::redis::client_options options;
    options.max_pending_commands = 2;
    client connection(options);
    check(connect(connection, endpoint.port()), "connected");
    std::atomic<unsigned> first_calls{};
    std::promise<result> first, second, overflow, third;
    auto first_future = first.get_future(), second_future = second.get_future();
    auto overflow_future = overflow.get_future(), third_future = third.get_future();
    auto id = connection.async_execute({"INCR", "first"}, [&](result reply) {
        ++first_calls;
        first.set_value(std::move(reply));
    });
    connection.async_execute({"INCR", "second"}, [&](result reply) { second.set_value(std::move(reply)); });
    await(seen);
    connection.cancel(id);
    auto cancelled = await(first_future);
    check(cancelled.error == client_errc::cancelled && cancelled.command_may_have_run, "unknown sent command outcome");
    connection.async_execute({"INCR", "overflow"}, [&](result reply) { overflow.set_value(std::move(reply)); });
    auto rejected = await(overflow_future);
    check(rejected.error == client_errc::queue_full && !rejected.command_may_have_run, "tombstone remains bounded");
    release.set_value();
    auto response = await(second_future);
    check(response && response.replies[0].integer == 22, "late cancelled response cannot complete next request");
    connection.async_execute({"INCR", "third"}, [&](result reply) { third.set_value(std::move(reply)); });
    response = await(third_future);
    check(response && response.replies[0].integer == 33, "drained request releases queue budget");
    endpoint.finish();
    check(first_calls == 1, "cancelled completion runs once");
}

DOCTEST_TEST_CASE("Redis timeouts drain replies without shifting later response correlation") {
    std::promise<void> release;
    auto allowed = release.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"GET", "slow"});
        remote.expect({"GET", "following"});
        if (allowed.wait_for(5s) != std::future_status::ready)
        {
            throw std::runtime_error("Missing timeout signal");
        }
        remote.send(":10\r\n:20\r\n");
    });
    client connection;
    check(connect(connection, endpoint.port()), "connected");
    std::promise<result> slow, following;
    auto slow_future = slow.get_future(), following_future = following.get_future();
    connection.async_execute({"GET", "slow"}, [&](result reply) { slow.set_value(std::move(reply)); }, 20ms);
    connection.async_execute({"GET", "following"}, [&](result reply) { following.set_value(std::move(reply)); });
    auto timed_out = await(slow_future);
    check(timed_out.error == client_errc::timeout && timed_out.command_may_have_run, "deadline has unknown outcome");
    release.set_value();
    auto response = await(following_future);
    check(response && response.replies[0].integer == 20, "following request receives its own reply");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis undrainable cancelled commands close the connection after a bounded deadline") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"GET", "slow"});
        remote.expect({"GET", "blocked"});
        remote.wait_closed();
    });
    arknet::redis::client_options options;
    options.drain_timeout = 30ms;
    client connection(options);
    check(connect(connection, endpoint.port()), "connected");
    std::promise<result> slow, blocked;
    auto slow_future = slow.get_future(), blocked_future = blocked.get_future();
    connection.async_execute({"GET", "slow"}, [&](result reply) { slow.set_value(std::move(reply)); }, 20ms);
    connection.async_execute({"GET", "blocked"}, [&](result reply) { blocked.set_value(std::move(reply)); });
    check(await(slow_future).error == client_errc::timeout, "request deadline");
    auto response = await(blocked_future);
    check(
        response.error == client_errc::timeout && response.command_may_have_run, "drain deadline fails queued request");
    check(!connection.is_connected(), "connection is terminal");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis disconnect and malformed replies fail outstanding commands exactly once") {
    for (bool malformed : {false, true})
    {
        server endpoint([&](peer& remote) {
            remote.hello();
            remote.expect({"SET", "key", "value"});
            if (malformed)
            {
                remote.send("?invalid\r\n");
            }
        });
        client connection;
        check(connect(connection, endpoint.port()), "connected");
        std::promise<result> completed;
        auto future = completed.get_future();
        std::atomic<unsigned> calls{};
        connection.async_execute({"SET", "key", "value"}, [&](result reply) {
            ++calls;
            completed.set_value(std::move(reply));
        });
        auto response = await(future);
        check(response.error == (malformed ? client_errc::protocol_error : client_errc::disconnected),
            "explicit failure");
        check(response.command_may_have_run, "write outcome is not invented after loss of transport");
        endpoint.finish();
        check(calls == 1, "one completion");
    }
}

DOCTEST_TEST_CASE("Redis owner destruction inside a completion aborts later slots safely") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"GET", "first"});
        remote.expect({"GET", "second"});
        remote.send(":1\r\n:2\r\n");
    });
    auto connection = std::make_unique<client>();
    check(connect(*connection, endpoint.port()), "connected");
    std::promise<result> first, second;
    auto first_future = first.get_future(), second_future = second.get_future();
    connection->async_execute({"GET", "first"}, [&](result reply) {
        connection.reset();
        first.set_value(std::move(reply));
    });
    connection->async_execute({"GET", "second"}, [&](result reply) { second.set_value(std::move(reply)); });
    check(await(first_future), "first response");
    auto response = await(second_future);
    check(response.error == client_errc::closed && response.command_may_have_run, "later slot is closed exactly once");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis connection deadlines include handshake and late submissions still complete") {
    server endpoint([](peer& remote) {
        remote.expect({"HELLO", "3"});
        remote.wait_closed();
    });
    arknet::redis::client_options options;
    options.connect_timeout = 25ms;
    client connection(options);
    auto response = connect(connection, endpoint.port());
    check(response.error == client_errc::timeout, "full handshake deadline");
    std::promise<result> late;
    auto future = late.get_future();
    connection.async_execute({"PING"}, [&](result reply) { late.set_value(std::move(reply)); });
    response = await(future);
    check(response.error == client_errc::closed && !response.command_may_have_run, "late submission is never sent");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis handshake errors retain server diagnostics and never fall back or replay") {
    server endpoint([](peer& remote) {
        remote.expect({"HELLO", "3"});
        remote.send("-ERR unsupported HELLO\r\n");
        remote.wait_closed();
    });
    client connection;
    auto response = connect(connection, endpoint.port());
    check(response.error == client_errc::handshake_failed, "explicit handshake failure");
    check(response.replies.size() == 1 && response.replies[0].text == "ERR unsupported HELLO", "server diagnostics");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis rejects malformed non-HELLO handshake replies") {
    for (auto protocol : {arknet::redis::protocol_version::resp2, arknet::redis::protocol_version::resp3})
    {
        server endpoint([&](peer& remote) {
            if (protocol == arknet::redis::protocol_version::resp2)
            {
                remote.expect({"AUTH", "password"});
                remote.expect({"CLIENT", "SETNAME", "test"});
                remote.expect({"PING"});
                remote.expect({"SELECT", "1"});
                remote.send(":1\r\n+BOGUS\r\n+PONG\r\n+OK\r\n");
            }
            else
            {
                remote.expect({"HELLO", "3", "AUTH", "default", "password", "SETNAME", "test"});
                remote.expect({"SELECT", "1"});
                remote.send("%1\r\n+proto\r\n:3\r\n+BOGUS\r\n");
            }
            remote.wait_closed();
        });
        client connection;
        arknet::redis::connect_options options;
        options.protocol = protocol;
        options.password = "password";
        options.client_name = "test";
        options.database = 1;
        check(connect(connection, endpoint.port(), options).error == client_errc::handshake_failed,
            "strict handshake reply types and values");
        endpoint.finish();
    }
}

DOCTEST_TEST_CASE("Redis closing during handshake cannot resurrect a ready connection") {
    server endpoint([](peer& remote) {
        remote.expect({"HELLO", "3"});
        remote.send(">1\r\n+closing\r\n%1\r\n+proto\r\n:3\r\n");
        remote.wait_closed();
    });
    client connection;
    connection.set_push_handler([&](auto) { connection.close(); });
    auto response = connect(connection, endpoint.port());
    check(
        response.error == client_errc::closed && !connection.is_connected(), "close wins over a buffered HELLO reply");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis invalid and oversized commands are rejected before sending") {
    arknet::redis::client_options options;
    options.max_pending_bytes = 32;
    client connection(options);
    std::promise<result> empty, large, disconnected, unsupported;
    auto unsupported_future = unsupported.get_future();
    auto empty_future = empty.get_future(), large_future = large.get_future(),
         disconnected_future = disconnected.get_future();
    connection.async_execute({}, [&](result reply) { empty.set_value(std::move(reply)); });
    connection.async_execute({"ECHO", std::string(100, 'x')}, [&](result reply) { large.set_value(std::move(reply)); });
    connection.async_execute({"PING"}, [&](result reply) { disconnected.set_value(std::move(reply)); });
    connection.async_execute(
        {"client", "reply", "off"}, [&](result reply) { unsupported.set_value(std::move(reply)); });
    check(await(empty_future).error == client_errc::invalid_command, "empty command");
    check(await(large_future).error == client_errc::queue_full, "encoded byte limit");
    auto response = await(disconnected_future);
    check(response.error == client_errc::not_connected && !response.command_may_have_run, "command requires handshake");
    check(await(unsupported_future).error == client_errc::unsupported_command,
        "response suppression cannot desynchronize slots");
}

DOCTEST_TEST_CASE("Redis pipeline response retention has an aggregate byte bound") {
    server endpoint([](peer& remote) {
        remote.hello();
        remote.expect({"GET", "first"});
        remote.expect({"GET", "second"});
        remote.send("$200\r\n" + std::string(200, 'x') + "\r\n$200\r\n" + std::string(200, 'y') + "\r\n");
    });
    arknet::redis::client_options options;
    options.max_pipeline_response_bytes = 4 * sizeof(arknet::redis::value);
    client connection(options);
    check(connect(connection, endpoint.port()), "connected within response budget");
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_pipeline(
        {{"GET", "first"}, {"GET", "second"}}, [&](result reply) { completed.set_value(std::move(reply)); });
    auto response = await(future);
    check(response.error == client_errc::response_limit, "aggregate bound includes all replies in a pipeline");
    check(
        response.replies.size() == 1 && response.replies[0].text == std::string(200, 'x'), "completed prefix retained");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis cancellation callback can release the last owner before arming a drain timer") {
    std::promise<void> received;
    auto seen = received.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"INCR", "key"});
        received.set_value();
        remote.wait_closed();
    });
    auto connection = std::make_unique<client>();
    check(connect(*connection, endpoint.port()), "connected");
    std::promise<result> completed;
    auto future = completed.get_future();
    auto id = connection->async_execute({"INCR", "key"}, [&](result reply) {
        connection.reset();
        completed.set_value(std::move(reply));
    });
    await(seen);
    connection->cancel(id);
    check(await(future).error == client_errc::cancelled, "reentrant cancellation teardown");
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis single-node client interoperates with an independent authenticated server" *
                  doctest::skip(std::getenv("ARKNET_REDIS_TEST_PORT") == nullptr)) {
    const char* port = std::getenv("ARKNET_REDIS_TEST_PORT");
    for (auto protocol : {arknet::redis::protocol_version::resp3, arknet::redis::protocol_version::resp2})
    {
        client connection;
        arknet::redis::connect_options options;
        options.protocol = protocol;
        options.password = "arknet-tests-only";
        options.client_name = "arknet-native-client-test";
        check(connect(connection, port, options), "real HELLO/AUTH handshake");
        const std::string binary("a\0\r\nb", 5);
        std::promise<result> completed;
        auto future = completed.get_future();
        connection.async_pipeline({{"SET", "arknet-client-integration", binary}, {"GET", "arknet-client-integration"},
                                      {"DEL", "arknet-client-integration"}},
            [&](result reply) { completed.set_value(std::move(reply)); });
        auto response = await(future);
        check(response && response.replies.size() == 3, "real pipeline");
        check(
            response.replies[0].text == "OK" && response.replies[1].text == binary && response.replies[2].integer == 1,
            "binary data roundtrip and cleanup");
    }
    client rejected;
    arknet::redis::connect_options options;
    options.password = "incorrect-test-password";
    auto response = connect(rejected, port, options);
    check(response.error == client_errc::handshake_failed && response.replies.size() == 1,
        "invalid authentication fails");
    check(response.replies[0].type == value_type::error, "authentication error remains available");
}

DOCTEST_TEST_CASE("Redis ACL authentication accepts an explicitly named nopass user" *
                  doctest::skip(std::getenv("ARKNET_REDIS_TEST_PORT") == nullptr)) {
    const char* port = std::getenv("ARKNET_REDIS_TEST_PORT");
    client administrator;
    arknet::redis::connect_options options;
    options.password = "arknet-tests-only";
    check(connect(administrator, port, options), "administrator authentication");
    std::promise<result> created;
    auto created_future = created.get_future();
    administrator.async_execute({"ACL", "SETUSER", "arknet-client-nopass", "on", "nopass", "~*", "+@all"},
        [&](result response) { created.set_value(std::move(response)); });
    check(await(created_future).replies[0].text == "OK", "create nopass test user");
    for (auto protocol : {arknet::redis::protocol_version::resp3, arknet::redis::protocol_version::resp2})
    {
        client named;
        arknet::redis::connect_options identity;
        identity.protocol = protocol;
        identity.username = "arknet-client-nopass";
        check(connect(named, port, identity), "explicit empty password is authenticated on the wire");
    }
    std::promise<result> removed;
    auto removed_future = removed.get_future();
    administrator.async_execute(
        {"ACL", "DELUSER", "arknet-client-nopass"}, [&](result response) { removed.set_value(std::move(response)); });
    check(await(removed_future).replies[0].integer == 1, "cleanup nopass test user");
}

#if defined(ARKNET_ENABLE_SSL)
DOCTEST_TEST_CASE("Redis TLS verifies the independent server certificate before authentication" *
                  doctest::skip(std::getenv("ARKNET_REDIS_TEST_TLS_PORT") == nullptr ||
                                std::getenv("ARKNET_REDIS_TEST_CA") == nullptr)) {
    const char* port = std::getenv("ARKNET_REDIS_TEST_TLS_PORT");
    arknet::redis::connect_options options;
    options.password = "arknet-tests-only";
    arknet::redis::tls_client untrusted;
    auto rejected = connect(untrusted, port, options);
    check(rejected.error == client_errc::connection_failed, "untrusted certificate rejected by default");
    arknet::redis::tls_client connection({}, std::getenv("ARKNET_REDIS_TEST_CA"));
    check(connect(connection, port, options), "trusted TLS handshake and authentication");
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_execute({"PING"}, [&](result reply) { completed.set_value(std::move(reply)); });
    auto response = await(future);
    check(response && response.replies[0].text == "PONG", "encrypted Redis command");
}

DOCTEST_TEST_CASE(
    "Redis TLS supplies client certificates for independently enforced mutual authentication" *
    doctest::skip(std::getenv("ARKNET_REDIS_TEST_MTLS_PORT") == nullptr ||
                  std::getenv("ARKNET_REDIS_TEST_CA") == nullptr || std::getenv("ARKNET_REDIS_TEST_CERT") == nullptr ||
                  std::getenv("ARKNET_REDIS_TEST_KEY") == nullptr)) {
    arknet::redis::client_options configuration;
    configuration.tls_certificate_authority = std::getenv("ARKNET_REDIS_TEST_CA");
    configuration.tls_certificate_file = std::getenv("ARKNET_REDIS_TEST_CERT");
    configuration.tls_private_key_file = std::getenv("ARKNET_REDIS_TEST_KEY");
    arknet::redis::connect_options options;
    options.password = "arknet-tests-only";
    const char* port = std::getenv("ARKNET_REDIS_TEST_MTLS_PORT");
    arknet::redis::tls_client missing_identity({}, configuration.tls_certificate_authority);
    check(!connect(missing_identity, port, options), "server rejects a missing client certificate");
    arknet::redis::tls_client connection(configuration);
    check(connect(connection, port, options), "mutual TLS and Redis authentication");
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_execute({"PING"}, [&](result reply) { completed.set_value(std::move(reply)); });
    auto response = await(future);
    check(response && response.replies[0].text == "PONG", "command over mutual TLS");
}
#endif
