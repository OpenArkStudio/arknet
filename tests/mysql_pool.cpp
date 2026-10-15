#include <arknet/mysql/pool.hpp>
#include "mysql_test_peer.hpp"

using namespace mysql_test;

namespace {

using mysql_pool = arknet::mysql::pool;
using arknet::mysql::pool_options;

pool_options pooled_options(const mysql_peer& peer) {
    pool_options options;
    options.max_connections = 1;
    options.max_waiters = 16;
    options.io_threads = 2;
    options.acquire_timeout = 2s;
    options.reset_timeout = 2s;
    options.connection = options_for(peer);
    return options;
}

mysql_pool::acquire_result acquire(mysql_pool& pool, std::chrono::milliseconds timeout = 0ms) {
    std::promise<mysql_pool::acquire_result> completion;
    auto future = completion.get_future();
    pool.async_acquire([&](mysql_pool::acquire_result result) { completion.set_value(std::move(result)); }, timeout);
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

query_result query(mysql_pool::lease& lease, std::string sql, query_options options = {}) {
    std::promise<query_result> completion;
    auto future = completion.get_future();
    lease.async_query(std::move(sql), [&](query_result result) { completion.set_value(std::move(result)); }, options);
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

prepare_result prepare(mysql_pool::lease& lease, std::string sql) {
    std::promise<prepare_result> completion;
    auto future = completion.get_future();
    lease.async_prepare(std::move(sql), [&](prepare_result result) { completion.set_value(std::move(result)); });
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

query_result execute(
    mysql_pool::lease& lease, const prepared_statement& statement, std::vector<field_value> parameters) {
    std::promise<query_result> completion;
    auto future = completion.get_future();
    lease.async_execute(
        statement, std::move(parameters), [&](query_result result) { completion.set_value(std::move(result)); });
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

asio::awaitable<void> wait_for_signal(std::future<void>& signal) {
    while (signal.wait_for(0ms) != std::future_status::ready)
    {
        asio::steady_timer pause(co_await asio::this_coro::executor);
        pause.expires_after(2ms);
        co_await pause.async_wait(asio::use_awaitable);
    }
}

} // namespace

DOCTEST_TEST_CASE("MySQL pool returns a connection only after a real server reset") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3before", 7));
        co_await one_integer(server, "1");
        CHECK(co_await receive_command(server) == std::string(1, '\x1f'));
        co_await server.send(ok_packet);
        CHECK(co_await receive_command(server) == std::string(1, '\3') + "USE `test-db`");
        co_await server.send(ok_packet);
        CHECK(co_await receive_command(server) == std::string("\3after", 6));
        co_await one_integer(server, "2");
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    auto first = acquire(pool);
    REQUIRE_MESSAGE(bool(first), first.status.message);
    CHECK(std::get<std::int64_t>(query(first.connection, "before").rows[0][0]) == 1);
    first.connection.release();
    CHECK_FALSE(bool(first.connection));
    CHECK(query(first.connection, "released").status.code == connection_errc::not_connected);
    auto second = acquire(pool);
    REQUIRE_MESSAGE(bool(second), second.status.message);
    CHECK(std::get<std::int64_t>(query(second.connection, "after").rows[0][0]) == 2);
    pool.close();
    CHECK_FALSE(bool(second.connection));
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool waiters cancel and expire without breaking FIFO lease ownership") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        for (int index = 0; index < 2; ++index)
        {
            CHECK(co_await receive_command(server) == std::string(1, '\x1f'));
            co_await server.send(ok_packet);
            CHECK(co_await receive_command(server) == std::string(1, '\3') + "USE `test-db`");
            co_await server.send(ok_packet);
        }
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    auto initial = acquire(pool);
    REQUIRE(bool(initial));
    std::promise<mysql_pool::acquire_result> cancelled, expired, first, second;
    auto cancelled_result = cancelled.get_future();
    auto expired_result = expired.get_future();
    auto first_result = first.get_future();
    auto second_result = second.get_future();
    const auto id = pool.async_acquire([&](auto result) { cancelled.set_value(std::move(result)); });
    pool.async_acquire([&](auto result) { expired.set_value(std::move(result)); }, 20ms);
    pool.async_acquire([&](auto result) { first.set_value(std::move(result)); });
    pool.async_acquire([&](auto result) { second.set_value(std::move(result)); });
    pool.cancel(id);
    REQUIRE(cancelled_result.wait_for(5s) == std::future_status::ready);
    CHECK(cancelled_result.get().status.code == connection_errc::cancelled);
    REQUIRE(expired_result.wait_for(5s) == std::future_status::ready);
    CHECK(expired_result.get().status.code == connection_errc::timeout);
    initial.connection.release();
    REQUIRE(first_result.wait_for(5s) == std::future_status::ready);
    auto first_lease = first_result.get();
    REQUIRE(bool(first_lease));
    CHECK(second_result.wait_for(0ms) != std::future_status::ready);
    first_lease.connection.release();
    REQUIRE(second_result.wait_for(5s) == std::future_status::ready);
    auto second_lease = second_result.get();
    REQUIRE(bool(second_lease));
    pool.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool release waits pending callbacks before resetting and assigning the next lease") {
    std::promise<void> received, release;
    auto received_result = received.get_future();
    auto released = release.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        received.set_value();
        co_await wait_for_signal(released);
        co_await one_integer(server, "42");
        CHECK(co_await receive_command(server) == std::string(1, '\x1f'));
        co_await server.send(ok_packet);
        CHECK(co_await receive_command(server) == std::string(1, '\3') + "USE `test-db`");
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    auto first = acquire(pool);
    REQUIRE(bool(first));
    std::promise<query_result> completion;
    auto future = completion.get_future();
    first.connection.async_query("SELECT 42", [&](auto result) { completion.set_value(std::move(result)); });
    REQUIRE(received_result.wait_for(5s) == std::future_status::ready);
    first.connection.release();
    std::promise<mysql_pool::acquire_result> next;
    auto next_result = next.get_future();
    pool.async_acquire([&](auto result) { next.set_value(std::move(result)); });
    CHECK(next_result.wait_for(20ms) != std::future_status::ready);
    release.set_value();
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    CHECK(bool(future.get()));
    REQUIRE(next_result.wait_for(5s) == std::future_status::ready);
    auto second = next_result.get();
    REQUIRE(bool(second));
    pool.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool reset deadline includes pending command drain") {
    std::promise<void> received;
    auto received_result = received.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        received.set_value();
        co_await server.wait_for_eof();
    });
    auto options = pooled_options(peer);
    options.reset_timeout = 30ms;
    mysql_pool pool(options);
    auto first = acquire(pool);
    REQUIRE(bool(first));
    std::promise<query_result> completion;
    auto future = completion.get_future();
    first.connection.async_query("slow", [&](auto result) { completion.set_value(std::move(result)); });
    REQUIRE(received_result.wait_for(5s) == std::future_status::ready);
    const auto started = std::chrono::steady_clock::now();
    first.connection.release();
    REQUIRE(future.wait_for(1s) == std::future_status::ready);
    const auto result = future.get();
    CHECK(result.status.code == connection_errc::cancelled);
    CHECK(result.status.outcome_unknown);
    CHECK(std::chrono::steady_clock::now() - started < 1s);
    pool.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool discards connections after rejected session reset") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string(1, '\x1f'));
        std::string error(1, '\xff');
        wire::append_integer(error, 1047, 2);
        error.append("#08S01Reset rejected");
        co_await server.send(error);
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    auto first = acquire(pool);
    REQUIRE(bool(first));
    first.connection.release();
    // The single-peer server deliberately never greets a replacement socket.
    // A reused old connection would incorrectly succeed instead of timing out.
    const auto next = acquire(pool, 60ms);
    CHECK(next.status.code == connection_errc::timeout);
    CHECK_FALSE(bool(next));
    pool.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool shutdown cancels active commands and queued acquires exactly once") {
    std::promise<void> received;
    auto received_result = received.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        received.set_value();
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    auto first = acquire(pool);
    REQUIRE(bool(first));
    std::atomic<int> command_calls{}, acquire_calls{};
    std::promise<query_result> completion;
    auto future = completion.get_future();
    first.connection.async_query("slow", [&](auto result) {
        ++command_calls;
        completion.set_value(std::move(result));
    });
    REQUIRE(received_result.wait_for(5s) == std::future_status::ready);
    std::promise<mysql_pool::acquire_result> waiting;
    auto waiting_result = waiting.get_future();
    pool.async_acquire([&](auto result) {
        ++acquire_calls;
        waiting.set_value(std::move(result));
    });
    pool.close();
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    CHECK(future.get().status.code == connection_errc::cancelled);
    REQUIRE(waiting_result.wait_for(5s) == std::future_status::ready);
    CHECK(waiting_result.get().status.code == connection_errc::cancelled);
    CHECK(query(first.connection, "closed").status.code == connection_errc::not_connected);
    CHECK(command_calls.load() == 1);
    CHECK(acquire_calls.load() == 1);
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool waiter admission remains bounded before executor dispatch") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await server.wait_for_eof();
    });
    auto options = pooled_options(peer);
    options.max_waiters = 2;
    mysql_pool pool(options);
    std::promise<void> entered, release;
    auto entered_result = entered.get_future();
    auto released = release.get_future();
    std::promise<mysql_pool::acquire_result> initial;
    auto initial_result = initial.get_future();
    pool.async_acquire([&](auto result) {
        entered.set_value();
        released.wait();
        initial.set_value(std::move(result));
    });
    REQUIRE(entered_result.wait_for(5s) == std::future_status::ready);
    std::vector<std::future<mysql_pool::acquire_result>> futures;
    for (int index = 0; index < 16; ++index)
    {
        auto promise = std::make_shared<std::promise<mysql_pool::acquire_result>>();
        futures.push_back(promise->get_future());
        pool.async_acquire([promise](auto result) { promise->set_value(std::move(result)); });
    }
    release.set_value();
    REQUIRE(initial_result.wait_for(5s) == std::future_status::ready);
    auto active = initial_result.get();
    REQUIRE(bool(active));
    for (std::size_t index = 2; index < futures.size(); ++index)
    {
        REQUIRE(futures[index].wait_for(5s) == std::future_status::ready);
        CHECK(futures[index].get().status.code == connection_errc::queue_full);
    }
    pool.close();
    for (std::size_t index = 0; index < 2; ++index)
    {
        REQUIRE(futures[index].wait_for(5s) == std::future_status::ready);
        CHECK(futures[index].get().status.code == connection_errc::cancelled);
    }
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool owner may be destroyed in its acquire callback") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await server.wait_for_eof();
    });
    auto pool = std::make_unique<mysql_pool>(pooled_options(peer));
    std::promise<mysql_pool::acquire_result> completion;
    auto future = completion.get_future();
    pool->async_acquire([&](auto result) {
        pool.reset();
        completion.set_value(std::move(result));
    });
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    const auto result = future.get();
    CHECK(result.status.code == connection_errc::none);
    CHECK_FALSE(bool(result.connection));
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL real pool resets transaction state and rejects escaped prepared handles" *
                  doctest::skip(std::getenv("ARKNET_MYSQL_TEST_PORT") == nullptr)) {
    pool_options options;
    options.connection = real_options();
    options.max_connections = 1;
    options.io_threads = 2;
    mysql_pool pool(options);
    auto first = acquire(pool);
    REQUIRE_MESSAGE(bool(first), first.status.message);
    REQUIRE(bool(query(first.connection, "SET @arknet_pool_test=123")));
    auto statement = prepare(first.connection, "SELECT ? AS value");
    REQUIRE_MESSAGE(bool(statement), statement.status.message);
    REQUIRE(bool(execute(first.connection, statement.statement, {std::int64_t(42)})));
    std::promise<query_result> begin;
    auto began = begin.get_future();
    first.connection.async_begin([&](auto result) { begin.set_value(std::move(result)); });
    REQUIRE(began.wait_for(5s) == std::future_status::ready);
    REQUIRE(bool(began.get()));
    REQUIRE(bool(query(first.connection, "USE information_schema")));
    first.connection.release();
    auto second = acquire(pool);
    REQUIRE_MESSAGE(bool(second), second.status.message);
    CHECK_FALSE(statement.statement.is_open());
    CHECK(execute(second.connection, statement.statement, {std::int64_t(1)}).status.code ==
          connection_errc::invalid_statement);
    const auto cleared = query(second.connection, "SELECT @arknet_pool_test, DATABASE()");
    REQUIRE_MESSAGE(bool(cleared), cleared.status.message);
    CHECK(std::holds_alternative<std::monostate>(cleared.rows[0][0]));
    CHECK(std::get<std::string>(cleared.rows[0][1]) == options.connection.database);
    CHECK((cleared.status_flags & 1) == 0);
    pool.close();
}

DOCTEST_TEST_CASE("MySQL real pool with no initial database discards returned connections" *
                  doctest::skip(std::getenv("ARKNET_MYSQL_TEST_PORT") == nullptr)) {
    pool_options options;
    options.connection = real_options();
    options.connection.database.clear();
    options.max_connections = 1;
    options.io_threads = 2;
    mysql_pool pool(options);
    auto first = acquire(pool);
    REQUIRE_MESSAGE(bool(first), first.status.message);
    const auto initial = query(first.connection, "SELECT DATABASE()");
    REQUIRE(bool(initial));
    CHECK(std::holds_alternative<std::monostate>(initial.rows[0][0]));
    REQUIRE(bool(query(first.connection, "USE information_schema")));
    first.connection.release();
    auto second = acquire(pool);
    REQUIRE_MESSAGE(bool(second), second.status.message);
    const auto restored = query(second.connection, "SELECT DATABASE()");
    REQUIRE(bool(restored));
    CHECK(std::holds_alternative<std::monostate>(restored.rows[0][0]));
    pool.close();
}

DOCTEST_TEST_CASE("MySQL pool acquire deadline includes time waiting for its executor") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    std::promise<void> entered, release;
    auto entered_result = entered.get_future();
    auto released = release.get_future();
    std::promise<mysql_pool::acquire_result> first, waiting;
    auto first_result = first.get_future();
    auto waiting_result = waiting.get_future();
    pool.async_acquire([&](auto result) {
        entered.set_value();
        released.wait();
        first.set_value(std::move(result));
    });
    REQUIRE(entered_result.wait_for(5s) == std::future_status::ready);
    pool.async_acquire([&](auto result) { waiting.set_value(std::move(result)); }, 20ms);
    std::this_thread::sleep_for(40ms);
    release.set_value();
    REQUIRE(first_result.wait_for(5s) == std::future_status::ready);
    auto active = first_result.get();
    REQUIRE(bool(active));
    REQUIRE(waiting_result.wait_for(5s) == std::future_status::ready);
    CHECK(waiting_result.get().status.code == connection_errc::timeout);
    pool.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool discards connections when database restoration fails or exceeds the return deadline") {
    for (const bool timeout : {false, true})
    {
        mysql_peer peer([timeout](mysql_peer& server) -> asio::awaitable<void> {
            co_await authenticate(server);
            CHECK(co_await receive_command(server) == std::string(1, '\x1f'));
            co_await server.send(ok_packet);
            CHECK(co_await receive_command(server) == std::string(1, '\3') + "USE `test-db`");
            if (!timeout)
            {
                std::string error(1, '\xff');
                wire::append_integer(error, 1049, 2);
                error.append("#42000Database no longer exists");
                co_await server.send(error);
            }
            co_await server.wait_for_eof();
        });
        auto options = pooled_options(peer);
        options.reset_timeout = 30ms;
        mysql_pool pool(options);
        auto first = acquire(pool);
        REQUIRE(bool(first));
        first.connection.release();
        const auto next = acquire(pool, 80ms);
        CHECK(next.status.code == connection_errc::timeout);
        CHECK_FALSE(bool(next));
        pool.close();
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL pool safely quotes the initial database identifier during restore") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string(1, '\x1f'));
        co_await server.send(ok_packet);
        CHECK(co_await receive_command(server) == std::string(1, '\3') + "USE `name``with'quote`");
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    auto options = pooled_options(peer);
    options.connection.database = "name`with'quote";
    mysql_pool pool(options);
    auto first = acquire(pool);
    REQUIRE(bool(first));
    first.connection.release();
    auto second = acquire(pool);
    REQUIRE(bool(second));
    pool.close();
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL real pool shares IO while preserving concurrent exclusive leases and replacement" *
                  doctest::skip(std::getenv("ARKNET_MYSQL_TEST_PORT") == nullptr)) {
    pool_options options;
    options.connection = real_options();
    options.max_connections = 2;
    options.io_threads = 2;
    mysql_pool pool(options);
    auto first = acquire(pool);
    auto second = acquire(pool);
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    const auto first_id = query(first.connection, "SELECT CONNECTION_ID()").rows[0][0];
    const auto second_id = query(second.connection, "SELECT CONNECTION_ID()").rows[0][0];
    CHECK(first_id != second_id);
    REQUIRE(bool(query(first.connection, "SET @arknet_lease=1")));
    REQUIRE(bool(query(second.connection, "SET @arknet_lease=2")));
    std::promise<mysql_pool::acquire_result> waiting;
    auto future = waiting.get_future();
    pool.async_acquire([&](auto result) { waiting.set_value(std::move(result)); });
    CHECK(future.wait_for(20ms) != std::future_status::ready);
    first.connection.release();
    REQUIRE(future.wait_for(5s) == std::future_status::ready);
    auto third = future.get();
    REQUIRE(bool(third));
    const auto third_id = query(third.connection, "SELECT CONNECTION_ID()").rows[0][0];
    CHECK(third_id == first_id);
    CHECK(std::holds_alternative<std::monostate>(query(third.connection, "SELECT @arknet_lease").rows[0][0]));
    CHECK(std::get<std::int64_t>(query(second.connection, "SELECT @arknet_lease").rows[0][0]) == 2);
    std::promise<query_result> completion;
    auto completed = completion.get_future();
    const auto id = third.connection.async_query(
        "SELECT SLEEP(0.02)", [&](auto result) { completion.set_value(std::move(result)); });
    // An old released owner cannot cancel work in a later lease.
    first.connection.cancel(id);
    REQUIRE(completed.wait_for(5s) == std::future_status::ready);
    CHECK(bool(completed.get()));
    third.connection.discard();
    auto replacement = acquire(pool);
    REQUIRE(bool(replacement));
    const auto replacement_id = query(replacement.connection, "SELECT CONNECTION_ID()").rows[0][0];
    CHECK(replacement_id != third_id);
    pool.close();
}

DOCTEST_TEST_CASE("MySQL pool rejects invalid limits before starting IO") {
    for (int setting = 0; setting < 5; ++setting)
    {
        pool_options options;
        switch (setting)
        {
        case 0:
            options.max_connections = 0;
            break;
        case 1:
            options.max_waiters = 0;
            break;
        case 2:
            options.io_threads = 0;
            break;
        case 3:
            options.acquire_timeout = 0ms;
            break;
        default:
            options.reset_timeout = 0ms;
            break;
        }
        CHECK_THROWS_AS(mysql_pool{options}, std::invalid_argument);
    }
}

DOCTEST_TEST_CASE("MySQL pool rejects unrepresentable configured deadlines before starting IO") {
    for (const auto timeout : {std::chrono::milliseconds::max(), -1ms, 0ms})
    {
        for (int setting = 0; setting < 3; ++setting)
        {
            pool_options options;
            if (setting == 0)
            {
                options.acquire_timeout = timeout;
            }
            else if (setting == 1)
            {
                options.reset_timeout = timeout;
            }
            else
            {
                options.connection.timeout = timeout;
            }
            CHECK_THROWS_AS(mysql_pool{options}, std::invalid_argument);
        }
    }
}

DOCTEST_TEST_CASE("MySQL pool validates acquire overrides and preserves zero as the configured default") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await server.wait_for_eof();
    });
    auto options = pooled_options(peer);
    options.max_waiters = 1;
    mysql_pool pool(options);
    const auto caller = std::this_thread::get_id();
    arknet::mysql::request_id previous = 0;
    for (const auto timeout : {std::chrono::milliseconds::max(), -1ms})
    {
        std::atomic<unsigned> callbacks{};
        std::promise<mysql_pool::acquire_result> completion;
        auto future = completion.get_future();
        const auto id = pool.async_acquire(
            [&](mysql_pool::acquire_result result) {
                CHECK(std::this_thread::get_id() != caller);
                ++callbacks;
                completion.set_value(std::move(result));
            },
            timeout);
        CHECK(id != 0);
        CHECK(id != previous);
        previous = id;
        REQUIRE(future.wait_for(5s) == std::future_status::ready);
        const auto result = future.get();
        CHECK(result.status.code == connection_errc::invalid_options);
        CHECK_FALSE(result.status.outcome_unknown);
        CHECK_FALSE(bool(result.connection));
        CHECK(callbacks.load() == 1);
    }
    auto acquired = acquire(pool, 0ms);
    REQUIRE(bool(acquired));
    acquired.connection.discard();
    pool.close();
    peer.finish();
}
