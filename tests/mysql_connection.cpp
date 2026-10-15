#include "mysql_test_peer.hpp"

using namespace mysql_test;

DOCTEST_TEST_CASE("MySQL connection authenticates fragmented native-password exchange and supports reuse") {
    connection client;
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
            co_await server.send(greeting(), true);
            check_response(co_await server.receive(), "mysql_native_password", false);
            co_await server.send(ok_packet, true);
            co_await server.wait_for_eof();
        });
        const auto result = connect(client, options_for(peer));
        CHECK_MESSAGE(bool(result), result.message);
        CHECK(client.is_open());
        CHECK(connect(client, options_for(peer)).code == connection_errc::already_started);
        client.close();
        CHECK_FALSE(client.is_open());
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL connection handles auth switch and coalesced SHA2 fast-auth completion") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await server.send(greeting());
        check_response(co_await server.receive(), "mysql_native_password", false);
        std::string request(1, '\xfe');
        request.append("caching_sha2_password");
        request.push_back('\0');
        request.append(challenge);
        request.push_back('\0');
        co_await server.send(request, true);
        const auto response = co_await server.receive();
        std::string expected;
        REQUIRE(wire::make_auth_response("caching_sha2_password", "secret", challenge, expected) ==
                wire::auth_status::complete);
        CHECK(response == expected);
        std::string combined;
        REQUIRE(wire::encode_message(std::string("\1\3", 2), server.sequence, combined));
        std::string final_ok;
        REQUIRE(wire::encode_message(ok_packet, server.sequence, final_ok));
        combined.append(final_ok);
        co_await server.write_raw(combined);
        co_await server.wait_for_eof();
    });
    connection client;
    const auto result = connect(client, options_for(peer));
    CHECK_MESSAGE(bool(result), result.message);
    CHECK(client.is_open());
    client.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL connection performs SSLRequest then verified TLS full SHA2 password authentication") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await server.send(greeting("caching_sha2_password", true), true);
        const auto ssl_request = co_await server.receive();
        CHECK(ssl_request.size() == 32);
        CHECK((static_cast<unsigned char>(ssl_request[1]) & 8) != 0);
        co_await server.upgrade();
        check_response(co_await server.receive(), "caching_sha2_password", true);
        co_await server.send(std::string("\1\4", 2));
        CHECK(co_await server.receive() == std::string("secret\0", 7));
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    connection client;
    const auto result = connect(client, options_for(peer, true));
    CHECK_MESSAGE(bool(result), result.message, " ", result.transport_error.message());
    CHECK(client.is_open());
    client.close();
    peer.finish();
    CHECK(peer.observed_sni == "localhost");
}

DOCTEST_TEST_CASE("MySQL TLS is required by default and plaintext full-password requests never send credentials") {
    DOCTEST_SUBCASE("missing server TLS capability") {
        mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
            co_await server.send(greeting());
            co_await server.wait_for_eof();
        });
        connection client;
        auto options = options_for(peer);
        options.tls = connect_options{}.tls;
        const auto result = connect(client, options);
        CHECK(result.code == connection_errc::tls_required);
        CHECK_FALSE(client.is_open());
        peer.finish();
    }
    DOCTEST_SUBCASE("explicit plaintext rejects full authentication") {
        mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
            co_await server.send(greeting("caching_sha2_password"));
            check_response(co_await server.receive(), "caching_sha2_password", false);
            co_await server.send(std::string("\1\4", 2));
            co_await server.wait_for_eof();
        });
        connection client;
        const auto result = connect(client, options_for(peer));
        CHECK(result.code == connection_errc::tls_required);
        CHECK_FALSE(client.is_open());
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL verifies certificate trust and DNS identity without downgrade") {
    for (const bool wrong_identity : {false, true})
    {
        mysql_peer peer(
            [](mysql_peer& server) -> asio::awaitable<void> {
                co_await server.send(greeting("caching_sha2_password", true));
                CHECK((co_await server.receive()).size() == 32);
                try
                { co_await server.upgrade(); }
                catch (const arknet::system_error& error)
                { CHECK(bool(error.code())); }
            },
            wrong_identity);
        connection client;
        auto options = options_for(peer, true);
        if (!wrong_identity)
        {
            // This separate client certificate cannot be a trust anchor for
            // the server certificate signed by tests/certs/ca.pem.
            options.ca_file = std::string(ARKNET_TEST_CERT_DIR) + "/client.pem";
        }
        const auto result = connect(client, options);
        CHECK(result.code == connection_errc::tls);
        CHECK(bool(result.transport_error));
        CHECK_FALSE(client.is_open());
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL reports server authentication rejection with code and SQLSTATE") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await server.send(greeting());
        co_await server.receive();
        std::string error(1, '\xff');
        wire::append_integer(error, 1045, 2);
        error.append("#28000Access denied");
        co_await server.send(error);
        co_await server.wait_for_eof();
    });
    connection client;
    const auto result = connect(client, options_for(peer));
    CHECK(result.code == connection_errc::authentication);
    CHECK(result.server_code == 1045);
    CHECK(result.sql_state == "28000");
    CHECK(result.message == "Access denied");
    CHECK_FALSE(client.is_open());
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL rejects malformed greetings, wrong sequence and unsupported authentication") {
    for (int scenario = 0; scenario < 4; ++scenario)
    {
        mysql_peer peer([scenario](mysql_peer& server) -> asio::awaitable<void> {
            if (scenario == 0)
            {
                server.sequence = 1;
                co_await server.send(greeting());
            }
            else if (scenario == 1)
            {
                co_await server.send(std::string("\x0a", 1));
            }
            else if (scenario == 2)
            {
                co_await server.send(greeting("mysql_clear_password"));
            }
            else
            {
                co_await server.send(greeting());
                co_await server.receive();
                co_await server.send(std::string("\1\3", 2));
            }
            co_await server.wait_for_eof();
        });
        connection client;
        const auto result = connect(client, options_for(peer));
        CHECK(result.code == (scenario == 2 ? connection_errc::unsupported_auth : connection_errc::protocol));
        CHECK_FALSE(client.is_open());
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL uses a single deadline across partial handshake progress") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await server.send(greeting());
        co_await server.receive();
        co_await server.wait_for_eof();
    });
    connection client;
    auto options = options_for(peer);
    options.timeout = 60ms;
    const auto started = std::chrono::steady_clock::now();
    const auto result = connect(client, options);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(result.code == connection_errc::timeout);
    CHECK(result.transport_error == asio::error::timed_out);
    CHECK(elapsed >= 40ms);
    CHECK(elapsed < 1s);
    CHECK_FALSE(client.is_open());
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL close cancels pending authentication exactly once and supports callback destruction") {
    for (const bool destroy_in_callback : {false, true})
    {
        std::promise<void> received;
        auto received_result = received.get_future();
        mysql_peer peer([&received](mysql_peer& server) -> asio::awaitable<void> {
            co_await server.send(greeting());
            co_await server.receive();
            received.set_value();
            co_await server.wait_for_eof();
        });
        auto client = std::make_unique<connection>();
        std::promise<connection_result> completion;
        auto result = completion.get_future();
        std::atomic<int> calls{};
        client->async_connect(options_for(peer), [&](connection_result value) {
            ++calls;
            if (destroy_in_callback)
            {
                client.reset();
            }
            completion.set_value(std::move(value));
        });
        REQUIRE(received_result.wait_for(5s) == std::future_status::ready);
        client->close();
        REQUIRE(result.wait_for(5s) == std::future_status::ready);
        CHECK(result.get().code == connection_errc::cancelled);
        if (client)
        {
            CHECK_FALSE(client->is_open());
        }
        peer.finish();
        CHECK(calls.load() == 1);
    }
}

DOCTEST_TEST_CASE("MySQL success callback may destroy its connection safely") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await server.send(greeting());
        co_await server.receive();
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    auto client = std::make_unique<connection>();
    std::promise<connection_result> completion;
    auto result = completion.get_future();
    client->async_connect(options_for(peer), [&](connection_result value) {
        CHECK(client->is_open());
        client.reset();
        completion.set_value(std::move(value));
    });
    REQUIRE(result.wait_for(5s) == std::future_status::ready);
    CHECK(bool(result.get()));
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL invalid options complete asynchronously on the owned IO lane") {
    connection client;
    auto options = connect_options{};
    options.host.clear();
    const auto caller = std::this_thread::get_id();
    std::promise<connection_result> completion;
    auto result = completion.get_future();
    client.async_connect(options, [&](connection_result value) {
        CHECK(std::this_thread::get_id() != caller);
        completion.set_value(std::move(value));
    });
    REQUIRE(result.wait_for(5s) == std::future_status::ready);
    CHECK(result.get().code == connection_errc::invalid_options);
    CHECK_FALSE(client.is_open());
}

DOCTEST_TEST_CASE("MySQL verified TLS accepts an IP identity without DNS SNI") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await server.send(greeting("caching_sha2_password", true));
        CHECK((co_await server.receive()).size() == 32);
        co_await server.upgrade();
        check_response(co_await server.receive(), "caching_sha2_password", true);
        co_await server.send(std::string("\1\3", 2));
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    connection client;
    auto options = options_for(peer, true);
    options.server_name.clear();
    const auto result = connect(client, options);
    CHECK_MESSAGE(bool(result), result.message, " ", result.transport_error.message());
    client.close();
    peer.finish();
    CHECK(peer.observed_sni.empty());
}

DOCTEST_TEST_CASE("MySQL close before queued resolve completes cancels once without reopening") {
    connection client;
    std::promise<void> entered;
    auto entered_result = entered.get_future();
    std::promise<void> release;
    auto released = release.get_future();
    auto invalid = connect_options{};
    invalid.host.clear();
    client.async_connect(invalid, [&](connection_result value) {
        CHECK(value.code == connection_errc::invalid_options);
        entered.set_value();
        released.wait();
    });
    REQUIRE(entered_result.wait_for(5s) == std::future_status::ready);
    std::promise<connection_result> completion;
    auto result = completion.get_future();
    std::atomic<int> calls{};
    auto options = connect_options{};
    options.host = "localhost";
    options.port = 1;
    client.async_connect(options, [&](connection_result value) {
        ++calls;
        completion.set_value(std::move(value));
    });
    client.close();
    release.set_value();
    REQUIRE(result.wait_for(5s) == std::future_status::ready);
    CHECK(result.get().code == connection_errc::cancelled);
    CHECK_FALSE(client.is_open());
    CHECK(calls.load() == 1);
}

// Set these variables to opt into a real, independently running MySQL server.
// The executable itself remains the only required test entry point.
DOCTEST_TEST_CASE("MySQL real server verified TLS and password rejection" *
                  doctest::skip(std::getenv("ARKNET_MYSQL_TEST_PORT") == nullptr)) {
    const auto environment = [](const char* name) -> std::string {
        const char* value = std::getenv(name);
        REQUIRE_MESSAGE(value != nullptr, name, " must be set for real MySQL tests");
        return value;
    };
    connect_options options;
    options.host = environment("ARKNET_MYSQL_TEST_HOST");
    options.port = static_cast<std::uint16_t>(std::stoul(environment("ARKNET_MYSQL_TEST_PORT")));
    options.username = environment("ARKNET_MYSQL_TEST_USER");
    options.password = environment("ARKNET_MYSQL_TEST_PASSWORD");
    options.database = environment("ARKNET_MYSQL_TEST_DATABASE");
    options.ca_file = environment("ARKNET_MYSQL_TEST_CA");
    options.timeout = 5s;
    connection client;
    // The server uses its uncached/full or cached/fast SHA2 path, according to
    // its own authentication cache. Both paths have separate byte-level tests.
    for (int index = 0; index < 2; ++index)
    {
        const auto result = connect(client, options);
        CHECK_MESSAGE(bool(result), result.message, " ", result.transport_error.message());
        CHECK(client.is_open());
        client.close();
    }
    options.password.append("-incorrect");
    const auto rejected = connect(client, options);
    CHECK(rejected.code == connection_errc::authentication);
    CHECK(rejected.server_code == 1045);
    CHECK(rejected.sql_state == "28000");
    CHECK_FALSE(client.is_open());
}

DOCTEST_TEST_CASE("MySQL queries reset sequence numbers and consume fragmented result boundaries") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3SELECT 42", 10));
        co_await one_integer(server, "42", true);
        CHECK(co_await receive_command(server) == std::string("\3UPDATE item SET value=1", 24));
        std::string ok("\0\1\0\2\0\0\0", 7);
        co_await server.send(ok, true);
        co_await server.wait_for_eof();
    });
    connection client;
    REQUIRE(bool(connect(client, options_for(peer))));
    const auto result = query(client, "SELECT 42");
    REQUIRE_MESSAGE(bool(result), result.status.message);
    REQUIRE(result.columns.size() == 1);
    CHECK(result.columns.front().name == "value");
    REQUIRE(result.rows.size() == 1);
    CHECK(std::get<std::int64_t>(result.rows.front().front()) == 42);
    CHECK(result.status_flags == 2);
    const auto update = query(client, "UPDATE item SET value=1");
    REQUIRE_MESSAGE(bool(update), update.status.message);
    CHECK(update.affected_rows == 1);
    client.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL server command errors leave a complete protocol boundary reusable") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        std::string error(1, '\xff');
        wire::append_integer(error, 1064, 2);
        error.append("#42000Syntax error");
        co_await server.send(error);
        co_await receive_command(server);
        co_await one_integer(server, "7");
        co_await server.wait_for_eof();
    });
    connection client;
    REQUIRE(bool(connect(client, options_for(peer))));
    const auto rejected = query(client, "invalid sql");
    CHECK(rejected.status.code == connection_errc::server);
    CHECK(rejected.status.server_code == 1064);
    CHECK(rejected.status.sql_state == "42000");
    CHECK_FALSE(rejected.status.outcome_unknown);
    CHECK(client.is_open());
    REQUIRE(bool(query(client, "SELECT 7")));
    client.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL result bounds close the socket before an incomplete response can be reused") {
    for (const bool rows_limit : {false, true})
    {
        mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
            co_await authenticate(server);
            co_await receive_command(server);
            co_await server.send(std::string(1, '\1'));
            co_await server.send(column_packet("value"));
            co_await server.send(eof_packet);
            co_await server.send(std::string("\x02"
                                             "42",
                3));
            co_await server.wait_for_eof();
        });
        connection client;
        REQUIRE(bool(connect(client, options_for(peer))));
        query_options options;
        if (rows_limit)
        {
            options.max_rows = 0;
        }
        else
        {
            options.max_result_bytes = 32;
        }
        const auto result = query(client, "SELECT 42", options);
        CHECK(result.status.code == connection_errc::result_limit);
        CHECK_FALSE(client.is_open());
        CHECK(query(client, "SELECT 1").status.code == connection_errc::not_connected);
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL queued cancellation and expiry preserve the active response correlation") {
    std::promise<void> first_received;
    auto received = first_received.get_future();
    std::promise<void> release;
    auto released = release.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3first", 6));
        first_received.set_value();
        while (released.wait_for(0ms) != std::future_status::ready)
        {
            asio::steady_timer pause(co_await asio::this_coro::executor);
            pause.expires_after(2ms);
            co_await pause.async_wait(asio::use_awaitable);
        }
        co_await one_integer(server, "1");
        CHECK(co_await receive_command(server) == std::string("\3last", 5));
        co_await one_integer(server, "2");
        co_await server.wait_for_eof();
    });
    connection client;
    REQUIRE(bool(connect(client, options_for(peer))));
    std::promise<query_result> first, cancelled, expired;
    auto first_result = first.get_future();
    auto cancelled_result = cancelled.get_future();
    auto expired_result = expired.get_future();
    client.async_query("first", [&](query_result value) { first.set_value(std::move(value)); });
    REQUIRE(received.wait_for(5s) == std::future_status::ready);
    const auto id = client.async_query("cancelled", [&](query_result value) { cancelled.set_value(std::move(value)); });
    query_options short_deadline;
    short_deadline.timeout = 20ms;
    client.async_query("expired", [&](query_result value) { expired.set_value(std::move(value)); }, short_deadline);
    client.cancel(id);
    REQUIRE(cancelled_result.wait_for(5s) == std::future_status::ready);
    const auto cancelled_value = cancelled_result.get();
    CHECK(cancelled_value.status.code == connection_errc::cancelled);
    CHECK_FALSE(cancelled_value.status.outcome_unknown);
    REQUIRE(expired_result.wait_for(5s) == std::future_status::ready);
    CHECK(expired_result.get().status.code == connection_errc::timeout);
    release.set_value();
    REQUIRE(first_result.wait_for(5s) == std::future_status::ready);
    CHECK(bool(first_result.get()));
    CHECK(bool(query(client, "last")));
    client.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL active cancellation and command deadlines close transport with unknown outcome") {
    for (const bool timeout : {false, true})
    {
        std::promise<void> request_received;
        auto received = request_received.get_future();
        mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
            co_await authenticate(server);
            co_await receive_command(server);
            request_received.set_value();
            co_await server.wait_for_eof();
        });
        connection client;
        REQUIRE(bool(connect(client, options_for(peer))));
        std::promise<query_result> completion;
        auto future = completion.get_future();
        query_options options;
        options.timeout = timeout ? 30ms : 2s;
        const auto id =
            client.async_commit([&](query_result value) { completion.set_value(std::move(value)); }, options);
        REQUIRE(received.wait_for(5s) == std::future_status::ready);
        if (!timeout)
        {
            client.cancel(id);
        }
        REQUIRE(future.wait_for(5s) == std::future_status::ready);
        const auto result = future.get();
        CHECK(result.status.code == (timeout ? connection_errc::timeout : connection_errc::cancelled));
        CHECK(result.status.outcome_unknown);
        CHECK_FALSE(client.is_open());
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL pending count and memory limits reject excess requests without sending") {
    for (const bool bytes_limit : {false, true})
    {
        std::promise<void> request_received;
        auto received = request_received.get_future();
        mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
            co_await authenticate(server);
            co_await receive_command(server);
            request_received.set_value();
            co_await server.wait_for_eof();
        });
        connection client;
        auto options = options_for(peer);
        options.max_pending_commands = bytes_limit ? 64 : 0;
        options.max_pending_bytes = bytes_limit ? 64 : 4096;
        REQUIRE(bool(connect(client, options)));
        std::promise<query_result> active;
        auto active_result = active.get_future();
        client.async_query("active", [&](query_result value) { active.set_value(std::move(value)); });
        REQUIRE(received.wait_for(5s) == std::future_status::ready);
        const auto rejected = query(client, std::string(32, 'x'));
        CHECK(rejected.status.code == connection_errc::queue_full);
        CHECK_FALSE(rejected.status.outcome_unknown);
        client.close();
        REQUIRE(active_result.wait_for(5s) == std::future_status::ready);
        CHECK(active_result.get().status.code == connection_errc::cancelled);
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL command callbacks may release the last connection owner") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        co_await one_integer(server, "42");
        co_await server.wait_for_eof();
    });
    auto client = std::make_unique<connection>();
    REQUIRE(bool(connect(*client, options_for(peer))));
    std::promise<query_result> completion;
    auto future = completion.get_future();
    client->async_query("SELECT 42", [&](query_result result) {
        client.reset();
        completion.set_value(std::move(result));
    });
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    CHECK(bool(future.get()));
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL real server queries prepared parameters transactions and session reset" *
                  doctest::skip(std::getenv("ARKNET_MYSQL_TEST_PORT") == nullptr)) {
    connection client;
    REQUIRE(bool(connect(client, real_options())));
    const std::string table =
        "arknet_connection_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    auto rows = query(client, "SELECT 42 AS number, 'hello' AS message, NULL AS absent");
    REQUIRE_MESSAGE(bool(rows), rows.status.message);
    REQUIRE(rows.columns.size() == 3);
    REQUIRE(rows.rows.size() == 1);
    CHECK(std::get<std::int64_t>(rows.rows[0][0]) == 42);
    CHECK(std::get<std::string>(rows.rows[0][1]) == "hello");
    CHECK(std::holds_alternative<std::monostate>(rows.rows[0][2]));
    REQUIRE(bool(query(client, "DROP TABLE IF EXISTS " + table)));
    REQUIRE(bool(query(client, "CREATE TABLE " + table +
                                   " (id BIGINT PRIMARY KEY, value VARBINARY(255), "
                                   "amount BIGINT) ENGINE=InnoDB")));
    auto prepared = prepare(client, "INSERT INTO " + table + " VALUES (?, ?, ?)");
    REQUIRE_MESSAGE(bool(prepared), prepared.status.message);
    CHECK(prepared.statement.parameter_count() == 3);
    const std::string binary("a\0b\xff", 4);
    auto inserted = execute(client, prepared.statement, {std::int64_t(1), binary, std::int64_t(-123)});
    REQUIRE_MESSAGE(bool(inserted), inserted.status.message);
    CHECK(inserted.affected_rows == 1);
    CHECK(execute(client, prepared.statement, {std::int64_t(2)}).status.code == connection_errc::invalid_options);
    auto selected = prepare(client, "SELECT id, value, amount FROM " + table + " WHERE id=?");
    REQUIRE_MESSAGE(bool(selected), selected.status.message);
    CHECK(selected.statement.columns().size() == 3);
    auto binary_rows = execute(client, selected.statement, {std::int64_t(1)});
    REQUIRE_MESSAGE(bool(binary_rows), binary_rows.status.message);
    REQUIRE(binary_rows.rows.size() == 1);
    CHECK(std::get<std::int64_t>(binary_rows.rows[0][0]) == 1);
    CHECK(std::get<std::string>(binary_rows.rows[0][1]) == binary);
    CHECK(std::get<std::int64_t>(binary_rows.rows[0][2]) == -123);
    CHECK(bool(close_statement(client, prepared.statement)));
    CHECK_FALSE(prepared.statement.is_open());
    CHECK(execute(client, prepared.statement, {}).status.code == connection_errc::invalid_statement);
    std::promise<query_result> begin, rollback, commit;
    auto began = begin.get_future();
    client.async_begin([&](query_result result) { begin.set_value(std::move(result)); });
    REQUIRE(began.wait_for(5s) == std::future_status::ready);
    REQUIRE(bool(began.get()));
    REQUIRE(bool(query(client, "UPDATE " + table + " SET amount=77 WHERE id=1")));
    auto rolled_back = rollback.get_future();
    client.async_rollback([&](query_result result) { rollback.set_value(std::move(result)); });
    REQUIRE(rolled_back.wait_for(5s) == std::future_status::ready);
    REQUIRE(bool(rolled_back.get()));
    CHECK(std::get<std::int64_t>(query(client, "SELECT amount FROM " + table + " WHERE id=1").rows[0][0]) == -123);
    std::promise<query_result> begin_again;
    auto began_again = begin_again.get_future();
    client.async_begin([&](query_result result) { begin_again.set_value(std::move(result)); });
    REQUIRE(began_again.wait_for(5s) == std::future_status::ready);
    REQUIRE(bool(began_again.get()));
    REQUIRE(bool(query(client, "UPDATE " + table + " SET amount=88 WHERE id=1")));
    auto committed = commit.get_future();
    client.async_commit([&](query_result result) { commit.set_value(std::move(result)); });
    REQUIRE(committed.wait_for(5s) == std::future_status::ready);
    REQUIRE(bool(committed.get()));
    CHECK(std::get<std::int64_t>(query(client, "SELECT amount FROM " + table + " WHERE id=1").rows[0][0]) == 88);
    REQUIRE(bool(query(client, "SET @arknet_connection_test=123")));
    REQUIRE(bool(reset(client)));
    CHECK_FALSE(selected.statement.is_open());
    CHECK(execute(client, selected.statement, {std::int64_t(1)}).status.code == connection_errc::invalid_statement);
    rows = query(client, "SELECT @arknet_connection_test");
    REQUIRE(bool(rows));
    CHECK(std::holds_alternative<std::monostate>(rows.rows[0][0]));
    REQUIRE(bool(query(client, "DROP TABLE " + table)));
    client.close();
}

DOCTEST_TEST_CASE("MySQL bounds producer admission while its IO lane is blocked in a callback") {
    std::promise<void> entered;
    auto entered_result = entered.get_future();
    std::promise<void> release;
    auto released = release.get_future();
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        co_await server.send(ok_packet);
        // Two 256KiB requests fit the 600KiB total admitted byte budget.
        for (int index = 0; index < 2; ++index)
        {
            server.sequence = 0;
            const auto request = co_await server.receive(300 * 1024);
            CHECK(request.size() == 256 * 1024 + 1);
            co_await server.send(ok_packet);
        }
        co_await server.wait_for_eof();
    });
    connection client;
    auto options = options_for(peer);
    options.max_pending_commands = 8;
    options.max_pending_bytes = 600 * 1024;
    REQUIRE(bool(connect(client, options)));
    client.async_query("block", [&](query_result result) {
        CHECK(bool(result));
        entered.set_value();
        released.wait();
    });
    REQUIRE(entered_result.wait_for(5s) == std::future_status::ready);
    std::vector<std::future<query_result>> futures;
    for (int index = 0; index < 16; ++index)
    {
        auto promise = std::make_shared<std::promise<query_result>>();
        futures.push_back(promise->get_future());
        client.async_query(
            std::string(256 * 1024, 'x'), [promise](query_result result) { promise->set_value(std::move(result)); });
    }
    release.set_value();
    int accepted = 0;
    int rejected = 0;
    for (auto& future : futures)
    {
        REQUIRE(future.wait_for(5s) == std::future_status::ready);
        const auto result = future.get();
        if (result)
        {
            ++accepted;
        }
        else
        {
            CHECK(result.status.code == connection_errc::queue_full);
            CHECK_FALSE(result.status.outcome_unknown);
            ++rejected;
        }
    }
    CHECK(accepted == 2);
    CHECK(rejected == 14);
    client.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL preserves initial server refusal before SQLSTATE negotiation") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        std::string refusal(1, '\xff');
        wire::append_integer(refusal, 1040, 2);
        refusal.append("Too many connections");
        co_await server.send(refusal, true);
        co_await server.wait_for_eof();
    });
    connection client;
    const auto result = connect(client, options_for(peer));
    CHECK(result.code == connection_errc::server);
    CHECK(result.server_code == 1040);
    CHECK(result.sql_state.empty());
    CHECK(result.message == "Too many connections");
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL last prepared handle automatically sends one no-response close before the next query") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        const auto prepare_request = co_await receive_command(server);
        CHECK(static_cast<unsigned char>(prepare_request.front()) == 0x16);
        std::string prepared(1, '\0');
        wire::append_integer(prepared, 77, 4);
        prepared.append(7, '\0');
        co_await server.send(prepared);
        CHECK(co_await receive_command(server) == std::string("\3between", 8));
        co_await server.send(ok_packet);
        const auto close_request = co_await receive_command(server);
        REQUIRE(close_request.size() == 5);
        CHECK(static_cast<unsigned char>(close_request.front()) == 0x19);
        CHECK(static_cast<unsigned char>(close_request[1]) == 77);
        // COM_STMT_CLOSE has no reply. The next command starts at sequence0.
        CHECK(co_await receive_command(server) == std::string("\3after", 6));
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    connection client;
    auto options = options_for(peer);
    options.max_pending_bytes = 1024;
    REQUIRE(bool(connect(client, options)));
    auto prepared = prepare(client, "DO 1");
    REQUIRE_MESSAGE(bool(prepared), prepared.status.message);
    std::vector<field_value> retained_storage;
    retained_storage.reserve(1000);
    CHECK(execute(client, prepared.statement, std::move(retained_storage)).status.code == connection_errc::queue_full);
    auto second_handle = prepared.statement;
    prepared.statement = {};
    REQUIRE(second_handle.is_open());
    REQUIRE(bool(query(client, "between")));
    second_handle = {};
    REQUIRE(bool(query(client, "after")));
    client.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL COMMIT transport loss reports unknown outcome and cancels unsent requests") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3COMMIT", 7));
        server.disconnect();
    });
    connection client;
    REQUIRE(bool(connect(client, options_for(peer))));
    std::promise<query_result> completion;
    auto future = completion.get_future();
    client.async_commit([&](query_result result) { completion.set_value(std::move(result)); });
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    const auto result = future.get();
    CHECK(result.status.code == connection_errc::transport);
    CHECK(result.status.outcome_unknown);
    CHECK_FALSE(client.is_open());
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL prepared handles cannot cross connections or survive reconnect") {
    mysql_peer first([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        std::string prepared(1, '\0');
        wire::append_integer(prepared, 78, 4);
        prepared.append(7, '\0');
        co_await server.send(prepared);
        co_await server.wait_for_eof();
    });
    mysql_peer second([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3valid", 6));
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    connection client;
    connection other;
    REQUIRE(bool(connect(client, options_for(first))));
    REQUIRE(bool(connect(other, options_for(second))));
    auto prepared = prepare(client, "DO 1");
    REQUIRE(bool(prepared));
    CHECK(execute(other, prepared.statement, {}).status.code == connection_errc::invalid_statement);
    REQUIRE(bool(query(other, "valid")));
    client.close();
    first.finish();
    mysql_peer replacement([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await server.wait_for_eof();
    });
    REQUIRE(bool(connect(client, options_for(replacement))));
    CHECK(execute(client, prepared.statement, {}).status.code == connection_errc::invalid_statement);
    CHECK_FALSE(prepared.statement.is_open());
    client.close();
    other.close();
    replacement.finish();
    second.finish();
}

DOCTEST_TEST_CASE("MySQL real server preserves typed binary parameters and exact full-size packet continuation" *
                  doctest::skip(std::getenv("ARKNET_MYSQL_TEST_PORT") == nullptr)) {
    connection client;
    REQUIRE(bool(connect(client, real_options())));
    auto prepared = prepare(client, "SELECT ? AS unsigned_value, ? AS bytes_value, ? AS absent, ? AS number, ? AS "
                                    "date_value, ? AS datetime_value, ? AS time_value");
    REQUIRE_MESSAGE(bool(prepared), prepared.status.message);
    const std::string bytes("s\0\xff", 3);
    const arknet::mysql::date_value date{2026, 10, 15};
    const arknet::mysql::datetime_value datetime{date, 10, 20, 30, 123456};
    const arknet::mysql::time_value time{true, 2, 3, 4, 5, 678901};
    auto result = execute(client, prepared.statement,
        {std::uint64_t(18446744073709551615ULL), bytes, std::monostate{}, 1.25, date, datetime, time});
    REQUIRE_MESSAGE(bool(result), result.status.message);
    REQUIRE(result.rows.size() == 1);
    REQUIRE(result.rows[0].size() == 7);
    CHECK(std::get<std::uint64_t>(result.rows[0][0]) == 18446744073709551615ULL);
    CHECK(std::get<std::string>(result.rows[0][1]) == bytes);
    CHECK(std::holds_alternative<std::monostate>(result.rows[0][2]));
    CHECK(std::get<double>(result.rows[0][3]) == 1.25);
    CHECK(std::get<arknet::mysql::date_value>(result.rows[0][4]) == date);
    CHECK(std::get<arknet::mysql::datetime_value>(result.rows[0][5]) == datetime);
    CHECK(std::get<arknet::mysql::time_value>(result.rows[0][6]) == time);
    result = query(client, "SELECT CAST('12345678901234567890.12345' AS DECIMAL(30,5))");
    REQUIRE_MESSAGE(bool(result), result.status.message);
    CHECK(std::get<std::string>(result.rows[0][0]) == "12345678901234567890.12345");
    // 0xfffffb data bytes + the four-byte length prefix exactly fill a
    // 0xffffff Classic payload, requiring the zero-size continuation packet.
    result = query(client, "SELECT REPEAT('x', 16777211)");
    REQUIRE_MESSAGE(bool(result), result.status.message);
    REQUIRE(result.rows.size() == 1);
    const auto& large = std::get<std::string>(result.rows[0][0]);
    CHECK(large.size() == 16777211);
    CHECK(large.front() == 'x');
    CHECK(large.back() == 'x');
    std::string large_request = "SELECT 1 /*";
    large_request.append(wire::max_packet_payload - 1 - large_request.size() - 2, 'x');
    large_request.append("*/");
    REQUIRE(large_request.size() + 1 == wire::max_packet_payload);
    REQUIRE(bool(query(client, std::move(large_request))));
    REQUIRE(bool(query(client, "SELECT 1")));
    REQUIRE(bool(close_statement(client, prepared.statement)));
    client.close();
}

DOCTEST_TEST_CASE("MySQL supports modern plugin auth without the obsolete secure-connection flag") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        std::string modern = greeting("caching_sha2_password");
        wire::server_greeting parsed;
        REQUIRE(wire::parse_greeting(modern, parsed) == wire::handshake_status::complete);
        // Locate the low capability word after protocol/version/id/nonce/filler.
        const auto capability_offset = 1 + parsed.server_version.size() + 1 + 4 + 8 + 1;
        modern[capability_offset + 1] &= static_cast<char>(0x7f);
        co_await server.send(modern);
        check_response(co_await server.receive(), "caching_sha2_password", false);
        co_await server.send(std::string("\1\3", 2));
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    connection client;
    const auto result = connect(client, options_for(peer));
    REQUIRE_MESSAGE(bool(result), result.message);
    client.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL rejects connection deadlines outside the steady-clock range") {
    for (const auto timeout : {std::chrono::milliseconds::max(), -1ms, 0ms})
    {
        connection client;
        connect_options options;
        options.timeout = timeout;
        const auto caller = std::this_thread::get_id();
        std::promise<connection_result> completion;
        auto future = completion.get_future();
        client.async_connect(options, [&](connection_result result) {
            CHECK(std::this_thread::get_id() != caller);
            completion.set_value(std::move(result));
        });
        REQUIRE(future.wait_for(5s) == std::future_status::ready);
        CHECK(future.get().code == connection_errc::invalid_options);
        CHECK_FALSE(client.is_open());
    }
}

DOCTEST_TEST_CASE("MySQL checked deadlines reject duration conversion and time-point overflow") {
    using clock = std::chrono::steady_clock;
    using arknet::detail::checked_deadline;
    const auto base = clock::time_point{};
    CHECK_FALSE(checked_deadline(base, std::chrono::milliseconds::max()));
    CHECK_FALSE(checked_deadline(base, std::chrono::milliseconds::min()));
    CHECK_FALSE(checked_deadline(base, -1ms));
    CHECK_FALSE(checked_deadline(base, 0ms));
    const auto short_deadline = checked_deadline(base, 1ms);
    REQUIRE(short_deadline.has_value());
    CHECK(*short_deadline == base + 1ms);
    CHECK(checked_deadline(clock::time_point::min(), 1ms).has_value());
    CHECK_FALSE(checked_deadline(clock::time_point::max(), 1ms));
    const auto near_max = clock::time_point::max() - 2ms;
    const auto within_range = checked_deadline(near_max, 1ms);
    REQUIRE(within_range.has_value());
    CHECK(*within_range == near_max + 1ms);
    CHECK_FALSE(checked_deadline(near_max, 3ms));
    const auto long_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(clock::duration::max()) - 1ms;
    CHECK(checked_deadline(base, long_timeout).has_value());
}

DOCTEST_TEST_CASE("MySQL invalid command deadlines preserve IDs callbacks and wire correlation") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string(1, '\3') + "valid");
        co_await one_integer(server, "42");
        co_await server.wait_for_eof();
    });
    connection client;
    REQUIRE(bool(connect(client, options_for(peer))));
    const auto caller = std::this_thread::get_id();
    arknet::mysql::request_id previous = 0;
    for (const auto timeout : {std::chrono::milliseconds::max(), -1ms, 0ms})
    {
        query_options options;
        options.timeout = timeout;
        std::atomic<unsigned> callbacks{};
        std::promise<query_result> completion;
        auto future = completion.get_future();
        const auto id = client.async_query(
            "must-not-send",
            [&](query_result result) {
                CHECK(std::this_thread::get_id() != caller);
                ++callbacks;
                completion.set_value(std::move(result));
            },
            options);
        CHECK(id != 0);
        CHECK(id != previous);
        previous = id;
        REQUIRE(future.wait_for(5s) == std::future_status::ready);
        const auto result = future.get();
        CHECK(result.status.code == connection_errc::invalid_options);
        CHECK_FALSE(result.status.outcome_unknown);
        CHECK(callbacks.load() == 1);
        CHECK(client.is_open());
    }
    query_options short_timeout;
    short_timeout.timeout = 500ms;
    const auto result = query(client, "valid", short_timeout);
    REQUIRE(bool(result));
    CHECK(std::get<std::int64_t>(result.rows[0][0]) == 42);
    client.close();
    peer.finish();
}
