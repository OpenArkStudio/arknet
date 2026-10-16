#if defined(_MSC_VER)
#include "scoped_asio_allocation_failure.hpp"

#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <arknet/mysql/pool.hpp>
#include "mysql_test_peer.hpp"
#include "scoped_allocation_failure.hpp"

using namespace mysql_test;

namespace {

using mysql_pool = arknet::mysql::pool;
using arknet::mysql::pool_options;

// A discarded first session must reach EOF. A different physical connection
// then authenticates and acknowledges exactly one fresh command; neither RESET
// nor replaying the original SQL is accepted on either session.
class replacement_peer
{
    asio::io_context context_;
    asio::ip::tcp::acceptor acceptor_{context_, {asio::ip::address_v4::loopback(), 0}};
    asio::steady_timer deadline_{context_};
    std::promise<void> completion_, release_reply_, query_received_;
    std::future<void> ready_ = completion_.get_future();
    std::shared_future<void> reply_ = release_reply_.get_future().share();
    std::future<void> received_ = query_received_.get_future();
    std::thread worker_;
    unsigned finished_{};

    static asio::awaitable<void> send(asio::ip::tcp::socket& socket, std::string payload, std::uint8_t sequence) {
        std::string bytes;
        REQUIRE(wire::encode_message(payload, sequence, bytes));
        co_await asio::async_write(socket, asio::buffer(bytes), asio::use_awaitable);
    }

    static asio::awaitable<std::string> receive(asio::ip::tcp::socket& socket, std::uint8_t sequence) {
        std::array<unsigned char, 4> header{};
        co_await asio::async_read(socket, asio::buffer(header), asio::use_awaitable);
        REQUIRE(header[3] == sequence);
        const auto size = std::size_t(header[0]) | (std::size_t(header[1]) << 8) | (std::size_t(header[2]) << 16);
        REQUIRE(size <= 4096);
        std::string payload(size, '\0');
        co_await asio::async_read(socket, asio::buffer(payload), asio::use_awaitable);
        co_return payload;
    }

    asio::awaitable<void> serve(asio::ip::tcp::socket socket, unsigned index) {
        co_await send(socket, greeting("mysql_native_password", false), 0);
        co_await receive(socket, 1);
        co_await send(socket, ok_packet, 2);
        const auto command = co_await receive(socket, 0);
        CHECK(command == (index ? std::string("\3fresh", 6) : std::string("\3written", 8)));
        if (!index)
        {
            ++original_writes;
            query_received_.set_value();
            asio::steady_timer pause(context_);
            while (reply_.wait_for(0ms) != std::future_status::ready)
            {
                pause.expires_after(1ms);
                co_await pause.async_wait(asio::use_awaitable);
            }
        }
        else
        {
            ++fresh_writes;
        }
        co_await send(socket, ok_packet, 1);
        std::array<char, 4> bytes{};
        error_code error;
        const auto size =
            co_await socket.async_read_some(asio::buffer(bytes), asio::redirect_error(asio::use_awaitable, error));
        CHECK(size == 0);
        CHECK((error == asio::error::eof || error == asio::error::connection_reset));
        if (!index)
        {
            old_closed = true;
        }
        else
        {
            fresh_closed = true;
        }
    }

public:
    std::atomic<unsigned> accepted{}, original_writes{}, fresh_writes{};
    std::atomic<bool> old_closed{}, fresh_closed{};

    replacement_peer() {
        deadline_.expires_after(5s);
        deadline_.async_wait([this](error_code error) {
            if (!error)
            {
                completion_.set_exception(std::make_exception_ptr(std::runtime_error("replacement peer deadline")));
                context_.stop();
            }
        });
        asio::co_spawn(
            context_,
            [this]() -> asio::awaitable<void> {
                for (unsigned index = 0; index != 2; ++index)
                {
                    auto socket = co_await acceptor_.async_accept(asio::use_awaitable);
                    ++accepted;
                    asio::co_spawn(context_, serve(std::move(socket), index), [this](std::exception_ptr error) {
                        if (error)
                        {
                            completion_.set_exception(error);
                            context_.stop();
                        }
                        else if (++finished_ == 2)
                        {
                            deadline_.cancel();
                            completion_.set_value();
                        }
                    });
                }
            },
            asio::detached);
        worker_ = std::thread([this] { context_.run(); });
    }

    ~replacement_peer() {
        context_.stop();
        worker_.join();
    }

    std::uint16_t port() const {
        return acceptor_.local_endpoint().port();
    }

    void allow_reply() {
        release_reply_.set_value();
    }

    void wait_for_query() {
        REQUIRE(received_.wait_for(5s) == std::future_status::ready);
    }

    void finish() {
        REQUIRE(ready_.wait_for(6s) == std::future_status::ready);
        ready_.get();
    }
};

pool_options configuration(const replacement_peer& peer) {
    pool_options options;
    options.max_connections = 1;
    options.io_threads = 1;
    options.acquire_timeout = 2s;
    options.reset_timeout = 2s;
    options.connection.host = "127.0.0.1";
    options.connection.port = peer.port();
    options.connection.username = "test";
    options.connection.password = "password";
    options.connection.database = "test-db";
    options.connection.tls = tls_mode::disabled;
    return options;
}

mysql_pool::acquire_result acquire(mysql_pool& pool) {
    std::promise<mysql_pool::acquire_result> completion;
    auto ready = completion.get_future();
    pool.async_acquire([&](auto result) { completion.set_value(std::move(result)); });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    return ready.get();
}

bool query(mysql_pool::lease& lease, const char* sql) {
    std::promise<bool> completion;
    auto ready = completion.get_future();
    lease.async_query(sql, [&](auto result) { completion.set_value(bool(result)); });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    return ready.get();
}

void wait_for_diagnostic(mysql_pool& pool, std::uint64_t count) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (pool.diagnostics().return_submission_failures < count && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    const auto diagnostic = pool.diagnostics();
    REQUIRE(diagnostic.return_submission_failures == count);
    REQUIRE(bool(diagnostic.last_return_submission_exception));
    CHECK_THROWS_AS(std::rethrow_exception(diagnostic.last_return_submission_exception), std::bad_alloc);
}

void verify_replacement(mysql_pool& pool, replacement_peer& peer, mysql_pool::acquire_result fresh) {
    REQUIRE(bool(fresh));
    CHECK(peer.accepted == 2);
    CHECK(query(fresh.connection, "fresh"));
    CHECK(peer.original_writes == 1);
    CHECK(peer.fresh_writes == 1);
    CHECK(pool.close_and_wait());
    CHECK_FALSE(fresh.connection);
    peer.finish();
    CHECK(peer.old_closed);
}

void verify_replacement(mysql_pool& pool, replacement_peer& peer) {
    verify_replacement(pool, peer, acquire(pool));
}

} // namespace

DOCTEST_TEST_CASE("MySQL lease destruction contains a failed return post and recovers a fresh session") {
    replacement_peer peer;
    mysql_pool pool(configuration(peer));
    CHECK(pool.diagnostics().return_submission_failures == 0);
    CHECK_FALSE(pool.diagnostics().last_return_submission_exception);
    auto held = acquire(pool);
    REQUIRE(bool(held));
    peer.allow_reply();
    CHECK(query(held.connection, "written"));
    const auto before = scoped_allocation_failure::injections.load();
    {
        scoped_allocation_failure::scope fail;
        held.connection = {};
    }
    CHECK(scoped_allocation_failure::injections == before + 1);
    wait_for_diagnostic(pool, 1);
    verify_replacement(pool, peer);
    CHECK(pool.diagnostics().return_submission_failures == 1);
}

DOCTEST_TEST_CASE("MySQL post-handler return failure preserves successful SQL and retires the session") {
    replacement_peer peer;
    mysql_pool pool(configuration(peer));
    auto held = acquire(pool);
    REQUIRE(bool(held));
    std::promise<bool> written;
    auto ready = written.get_future();
    const auto before = scoped_allocation_failure::injections.load();
    held.connection.async_query("written", [&](auto result) {
        written.set_value(bool(result));
        // finish() runs immediately after this handler. No fake executor hook
        // is involved: its first actual allocation is the return post.
        scoped_allocation_failure::arm();
    });
    peer.wait_for_query();
    held.connection.release();
    peer.allow_reply();
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    CHECK(ready.get());
    wait_for_diagnostic(pool, 1);
    CHECK(scoped_allocation_failure::injections == before + 1);
    verify_replacement(pool, peer);
    CHECK(pool.diagnostics().return_submission_failures == 1);
}

DOCTEST_TEST_CASE("MySQL failed recovery close stays indexed and preserves the original acquire deadline") {
    replacement_peer peer;
    mysql_pool pool(configuration(peer));
    auto held = acquire(pool);
    REQUIRE(bool(held));
    peer.allow_reply();
    CHECK(query(held.connection, "written"));
    std::promise<void> entered, resume;
    auto entered_ready = entered.get_future();
    auto resume_ready = resume.get_future().share();
    pool.async_acquire(
        [&](auto result) {
            CHECK(result.status.code == connection_errc::invalid_options);
            entered.set_value();
            resume_ready.wait();
            // The next already-queued acquire enters failed-entry recovery before
            // allocating its waiter timer; only the real child close post fails.
            scoped_allocation_failure::arm();
        },
        -1ms);
    REQUIRE(entered_ready.wait_for(5s) == std::future_status::ready);
    const auto before = scoped_allocation_failure::injections.load();
    {
        scoped_allocation_failure::scope fail;
        held.connection = {};
    }
    CHECK(scoped_allocation_failure::injections == before + 1);
    wait_for_diagnostic(pool, 1);
    std::promise<mysql_pool::acquire_result> waiting;
    auto ready = waiting.get_future();
    const auto submitted = std::chrono::steady_clock::now();
    pool.async_acquire([&](auto result) { waiting.set_value(std::move(result)); }, 250ms);
    resume.set_value();
    wait_for_diagnostic(pool, 2);
    CHECK(scoped_allocation_failure::injections == before + 2);
    CHECK(peer.accepted == 1);
    CHECK_FALSE(peer.old_closed);
    REQUIRE(ready.wait_for(3s) == std::future_status::ready);
    const auto result = ready.get();
    CHECK(result.status.code == connection_errc::timeout);
    CHECK_FALSE(result.connection);
    CHECK(std::chrono::steady_clock::now() - submitted < 2s);
    verify_replacement(pool, peer);
    CHECK(pool.diagnostics().return_submission_failures == 2);
}

DOCTEST_TEST_CASE("MySQL ordinary pool shutdown retains a failed child and closes remaining children") {
    replacement_peer peer;
    auto options = configuration(peer);
    options.max_connections = 2;
    mysql_pool pool(options);
    auto first = acquire(pool);
    auto second = acquire(pool);
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    peer.allow_reply();
    CHECK(query(first.connection, "written"));
    CHECK(query(second.connection, "fresh"));
    std::promise<void> entered, resume;
    auto entered_ready = entered.get_future();
    auto resume_ready = resume.get_future().share();
    pool.async_acquire(
        [&](auto result) {
            CHECK(result.status.code == connection_errc::invalid_options);
            entered.set_value();
            resume_ready.wait();
            scoped_allocation_failure::arm();
        },
        -1ms);
    REQUIRE(entered_ready.wait_for(5s) == std::future_status::ready);
    const auto before = scoped_allocation_failure::injections.load();
    pool.close();
    resume.set_value();
    wait_for_diagnostic(pool, 1);
    CHECK(scoped_allocation_failure::injections == before + 1);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!peer.fresh_closed && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(peer.fresh_closed);
    CHECK_FALSE(peer.old_closed);
    CHECK(peer.accepted == 2);
    // The failed first entry stays in the actor index and weak-child catalog;
    // permanent finalization must still close it and retire both old leases.
    CHECK(pool.close_and_wait());
    CHECK_FALSE(first.connection);
    CHECK_FALSE(second.connection);
    peer.finish();
    CHECK(peer.old_closed);
    CHECK(pool.diagnostics().return_submission_failures == 1);
}

DOCTEST_TEST_CASE("MySQL cancellation without a live ID recovers a failed return for an existing waiter") {
    replacement_peer peer;
    mysql_pool pool(configuration(peer));
    auto held = acquire(pool);
    REQUIRE(bool(held));
    peer.allow_reply();
    CHECK(query(held.connection, "written"));
    std::promise<mysql_pool::acquire_result> waiting;
    auto ready = waiting.get_future();
    pool.async_acquire([&](auto result) { waiting.set_value(std::move(result)); });
    std::promise<void> fenced;
    auto fenced_ready = fenced.get_future();
    // Posted after the valid acquire: when this callback runs, that waiter is
    // already queued behind the held lease and its original deadline is armed.
    pool.async_acquire(
        [&](auto result) {
            CHECK(result.status.code == connection_errc::invalid_options);
            fenced.set_value();
        },
        -1ms);
    REQUIRE(fenced_ready.wait_for(5s) == std::future_status::ready);
    CHECK(ready.wait_for(0ms) == std::future_status::timeout);
    const auto before = scoped_allocation_failure::injections.load();
    {
        scoped_allocation_failure::scope fail;
        held.connection = {};
    }
    CHECK(scoped_allocation_failure::injections == before + 1);
    wait_for_diagnostic(pool, 1);
    pool.cancel(0);
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    auto replacement = ready.get();
    REQUIRE(bool(replacement));
    verify_replacement(pool, peer, std::move(replacement));
    CHECK(pool.diagnostics().return_submission_failures == 1);
}
