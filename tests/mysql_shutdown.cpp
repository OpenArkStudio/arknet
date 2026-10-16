#include <atomic>
#include <cstdint>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "mysql_test_peer.hpp"

using namespace mysql_test;

namespace {

struct destruction_probe
{
    std::atomic<unsigned>& calls;

    ~destruction_probe() {
        ++calls;
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

std::string prepare_ok(std::uint32_t id) {
    std::string payload(1, '\0');
    wire::append_integer(payload, id, 4);
    payload.append(7, '\0');
    return payload;
}

} // namespace

DOCTEST_TEST_CASE("MySQL permanent shutdown works before connect and rejects future work synchronously") {
    connection client;
    CHECK(client.close_and_wait());
    CHECK_FALSE(client.is_open());
    CHECK(client.close_and_wait());
    const auto caller = std::this_thread::get_id();
    unsigned completions{};
    auto verify = [&](connection_result status) {
        CHECK(std::this_thread::get_id() == caller);
        CHECK(status.code == connection_errc::not_connected);
        CHECK_FALSE(status.outcome_unknown);
        ++completions;
    };
    client.async_connect({}, verify);
    CHECK(client.async_query("SELECT 1", [&](query_result result) { verify(std::move(result.status)); }) == 0);
    CHECK(client.async_prepare("SELECT ?", [&](prepare_result result) { verify(std::move(result.status)); }) == 0);
    CHECK(client.async_execute({}, {}, [&](query_result result) { verify(std::move(result.status)); }) == 0);
    CHECK(client.async_close({}, verify) == 0);
    CHECK(client.async_reset(verify) == 0);
    CHECK(client.async_begin([&](query_result result) { verify(std::move(result.status)); }) == 0);
    CHECK(client.async_commit([&](query_result result) { verify(std::move(result.status)); }) == 0);
    CHECK(client.async_rollback([&](query_result result) { verify(std::move(result.status)); }) == 0);
    CHECK(completions == 9);
    client.cancel(1);
    client.close();
    CHECK(client.close_and_wait());
    CHECK_THROWS_AS(
        client.async_query("closed", [](auto) { throw std::runtime_error("caller rejection"); }), std::runtime_error);
    CHECK(client.close_and_wait());
}

DOCTEST_TEST_CASE("MySQL permanent shutdown retires cancelled authentication captures and joins its worker") {
    for (const bool encrypted : {false, true})
    {
        std::promise<void> received;
        auto ready = received.get_future();
        mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
            co_await server.send(greeting("mysql_native_password", encrypted));
            if (encrypted)
            {
                CHECK((co_await server.receive()).size() == 32);
                co_await server.upgrade();
            }
            co_await server.receive();
            received.set_value();
            co_await server.wait_for_eof();
        });
        connection client;
        std::atomic<unsigned> destroyed{};
        auto capture = std::make_shared<destruction_probe>(destroyed);
        auto exited = std::make_shared<std::atomic<bool>>(false);
        std::atomic<unsigned> completions{};
        connection_result result;
        client.async_connect(options_for(peer, encrypted), [&, capture, exited](connection_result status) {
            record_worker_exit(exited);
            result = std::move(status);
            ++completions;
        });
        capture.reset();
        REQUIRE(ready.wait_for(5s) == std::future_status::ready);
        CHECK(client.close_and_wait());
        CHECK(result.code == connection_errc::cancelled);
        CHECK(completions == 1);
        CHECK(destroyed == 1);
        CHECK(exited->load());
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL permanent shutdown retires active and queued command captures once") {
    std::promise<void> received;
    auto ready = received.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3active", 7));
        received.set_value();
        co_await server.wait_for_eof();
    });
    connection client;
    REQUIRE(bool(connect(client, options_for(peer))));
    std::atomic<unsigned> destroyed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    auto exited = std::make_shared<std::atomic<bool>>(false);
    connection_result active, queued;
    std::atomic<unsigned> completions{};
    client.async_query("active", [&, capture, exited](query_result response) {
        record_worker_exit(exited);
        active = std::move(response.status);
        ++completions;
    });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    client.async_query("queued", [&, capture](query_result response) {
        queued = std::move(response.status);
        ++completions;
    });
    capture.reset();
    CHECK(client.close_and_wait());
    CHECK(active.code == connection_errc::cancelled);
    CHECK(active.outcome_unknown);
    CHECK(queued.code == connection_errc::cancelled);
    CHECK_FALSE(queued.outcome_unknown);
    CHECK(completions == 2);
    CHECK(destroyed == 1);
    CHECK(exited->load());
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL IO-thread permanent shutdown rejects without terminating the connection") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3still open", 11));
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    connection client;
    std::promise<bool> checked;
    auto ready = checked.get_future();
    client.async_connect(options_for(peer), [&](connection_result result) {
        CHECK(bool(result));
        const auto closed = client.close_and_wait();
        checked.set_value(!closed && arknet::get_last_error() == asio::error::operation_not_supported);
    });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    CHECK(ready.get());
    CHECK(bool(query(client, "still open")));
    CHECK(client.close_and_wait());
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL ordinary close still permits reconnect before permanent shutdown") {
    connection client;
    for (unsigned iteration = 0; iteration != 2; ++iteration)
    {
        mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
            co_await authenticate(server);
            co_await server.wait_for_eof();
        });
        REQUIRE(bool(connect(client, options_for(peer))));
        CHECK(client.is_open());
        if (!iteration)
        {
            client.close();
        }
        else
        {
            CHECK(client.close_and_wait());
        }
        peer.finish();
    }
    CHECK_FALSE(client.is_open());
    unsigned calls{};
    client.async_connect({}, [&](connection_result result) {
        CHECK(result.code == connection_errc::not_connected);
        ++calls;
    });
    CHECK(calls == 1);
}

DOCTEST_TEST_CASE("MySQL permanent shutdown leaves retained current and previously closed statements inert") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\x16"
                                                              "first",
                                                      6));
        co_await server.send(prepare_ok(70));
        CHECK(co_await receive_command(server) == std::string("\x19\x46\0\0\0", 5));
        CHECK(co_await receive_command(server) == std::string("\x16"
                                                              "second",
                                                      7));
        co_await server.send(prepare_ok(71));
        co_await server.wait_for_eof();
    });
    connection client;
    REQUIRE(bool(connect(client, options_for(peer))));
    auto first = prepare(client, "first");
    REQUIRE(bool(first));
    REQUIRE(bool(close_statement(client, first.statement)));
    auto second = prepare(client, "second");
    REQUIRE(bool(second));
    CHECK(second.statement.is_open());
    CHECK(client.close_and_wait());
    CHECK_FALSE(first.statement.is_open());
    CHECK_FALSE(second.statement.is_open());
    CHECK(client.async_close(second.statement,
              [](connection_result result) { CHECK(result.code == connection_errc::not_connected); }) == 0);
    client.cancel(1);
    std::thread destroy([first = std::move(first.statement), second = std::move(second.statement)]() mutable {
        first = {};
        second = {};
    });
    destroy.join();
    CHECK(client.close_and_wait());
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL permanent shutdown tolerates facade destruction in a cancelled callback") {
    std::promise<void> received;
    auto ready = received.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        received.set_value();
        co_await server.wait_for_eof();
    });
    auto client = std::make_unique<connection>();
    REQUIRE(bool(connect(*client, options_for(peer))));
    client->async_query("active", [&](query_result) { client.reset(); });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    auto* active = client.get();
    CHECK(active->close_and_wait());
    CHECK_FALSE(bool(client));
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL concurrent submit and permanent shutdown completes each caller exactly once") {
    connection client;
    std::atomic<unsigned> completions{};
    std::latch start(1);
    std::vector<std::thread> submitters;
    for (unsigned producer = 0; producer != 2; ++producer)
    {
        submitters.emplace_back([&] {
            start.wait();
            for (unsigned request = 0; request != 128; ++request)
            {
                client.async_query("not connected", [&](query_result response) {
                    CHECK(response.status.code == connection_errc::not_connected);
                    ++completions;
                });
                client.cancel(request + 1);
            }
        });
    }
    start.count_down();
    CHECK(client.close_and_wait());
    for (auto& submitter : submitters)
    {
        submitter.join();
    }
    CHECK(completions == 256);
    CHECK(client.close_and_wait());
}

DOCTEST_TEST_CASE("MySQL concurrent permanent shutdown waiters observe complete handler destruction") {
    connection client;
    std::atomic<unsigned> destroyed{};
    std::atomic<unsigned> completions{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    client.async_query("not connected", [&, capture](query_result) { ++completions; });
    capture.reset();
    std::latch start(1);
    std::atomic<unsigned> closed{};
    std::vector<std::thread> waiters;
    for (unsigned index = 0; index != 3; ++index)
    {
        waiters.emplace_back([&] {
            start.wait();
            if (client.close_and_wait())
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

DOCTEST_TEST_CASE("MySQL permanent shutdown does not stop another independent connection") {
    mysql_peer first([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await server.wait_for_eof();
    });
    mysql_peer second([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3unaffected", 11));
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    connection stopped, other;
    REQUIRE(bool(connect(stopped, options_for(first))));
    REQUIRE(bool(connect(other, options_for(second))));
    CHECK(stopped.close_and_wait());
    CHECK(other.is_open());
    CHECK(bool(query(other, "unaffected")));
    CHECK(other.close_and_wait());
    first.finish();
    second.finish();
}
