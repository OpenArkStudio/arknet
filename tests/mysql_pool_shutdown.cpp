#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <future>
#include <latch>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arknet/mysql/pool.hpp>
#include "mysql_test_peer.hpp"

using namespace mysql_test;

namespace {

using mysql_pool = arknet::mysql::pool;
using arknet::mysql::pool_options;

pool_options pooled_options(const mysql_peer& peer, bool encrypted = false) {
    pool_options options;
    options.max_connections = 1;
    options.max_waiters = 512;
    options.io_threads = 2;
    options.connection = options_for(peer, encrypted);
    options.connection.max_pending_commands = 512;
    return options;
}

mysql_pool::acquire_result acquire(mysql_pool& pool) {
    std::promise<mysql_pool::acquire_result> completion;
    auto ready = completion.get_future();
    pool.async_acquire([&](auto result) { completion.set_value(std::move(result)); });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    return ready.get();
}

prepare_result prepare(mysql_pool::lease& lease, std::string sql) {
    std::promise<prepare_result> completion;
    auto ready = completion.get_future();
    lease.async_prepare(std::move(sql), [&](auto result) { completion.set_value(std::move(result)); });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    return ready.get();
}

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
    std::string bytes(1, '\0');
    wire::append_integer(bytes, id, 4);
    bytes.append(7, '\0');
    return bytes;
}

// Two simultaneous physical children share one pool runtime. The minimal peer
// accepts the usual native-password handshake and acknowledges text commands.
class paired_peer
{
    asio::io_context context_;
    asio::ip::tcp::acceptor acceptor_{context_, {asio::ip::address_v4::loopback(), 0}};
    asio::steady_timer deadline_{context_};
    std::promise<void> completion_;
    std::future<void> ready_ = completion_.get_future();
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

    asio::awaitable<void> serve(asio::ip::tcp::socket socket) {
        co_await send(socket, greeting("mysql_native_password", false), 0);
        co_await receive(socket, 1);
        co_await send(socket, ok_packet, 2);
        try
        {
            for (;;)
            {
                const auto command = co_await receive(socket, 0);
                CHECK(command == std::string("\3other lease", 12));
                co_await send(socket, ok_packet, 1);
            }
        }
        catch (const arknet::system_error& exception)
        { CHECK((exception.code() == asio::error::eof || exception.code() == asio::error::connection_reset)); }
    }

public:
    paired_peer() {
        deadline_.expires_after(5s);
        deadline_.async_wait([this](error_code error) {
            if (!error)
            {
                completion_.set_exception(std::make_exception_ptr(std::runtime_error("paired peer deadline")));
                context_.stop();
            }
        });
        asio::co_spawn(
            context_,
            [this]() -> asio::awaitable<void> {
                for (unsigned index = 0; index != 2; ++index)
                {
                    auto socket = co_await acceptor_.async_accept(asio::use_awaitable);
                    asio::co_spawn(context_, serve(std::move(socket)), [this](std::exception_ptr error) {
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

    ~paired_peer() {
        context_.stop();
        worker_.join();
    }

    std::uint16_t port() const {
        return acceptor_.local_endpoint().port();
    }

    void finish() {
        REQUIRE(ready_.wait_for(6s) == std::future_status::ready);
        ready_.get();
    }
};

} // namespace

DOCTEST_TEST_CASE("MySQL pool permanent shutdown rejects future acquisition synchronously") {
    mysql_pool pool;
    CHECK(pool.close_and_wait());
    CHECK(pool.close_and_wait());
    const auto caller = std::this_thread::get_id();
    unsigned calls{};
    CHECK(pool.async_acquire([&](auto result) {
        CHECK(std::this_thread::get_id() == caller);
        CHECK(result.status.code == connection_errc::not_connected);
        CHECK_FALSE(result.connection);
        ++calls;
    }) == 0);
    CHECK(calls == 1);
    pool.cancel(1);
    pool.close();
    CHECK_THROWS_AS(pool.async_acquire([](auto) { throw std::runtime_error("rejected acquire"); }), std::runtime_error);
    CHECK(pool.close_and_wait());
}

DOCTEST_TEST_CASE("MySQL pool permanent shutdown cancels authentication and queued waiters before joining") {
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
        mysql_pool pool(pooled_options(peer, encrypted));
        std::atomic<unsigned> calls{}, destroyed{};
        auto capture = std::make_shared<destruction_probe>(destroyed);
        auto exited = std::make_shared<std::atomic<bool>>(false);
        for (unsigned index = 0; index != 2; ++index)
        {
            pool.async_acquire([&, capture, exited](auto result) {
                record_worker_exit(exited);
                CHECK(result.status.code == connection_errc::cancelled);
                CHECK_FALSE(result.connection);
                ++calls;
            });
        }
        capture.reset();
        REQUIRE(ready.wait_for(5s) == std::future_status::ready);
        CHECK(pool.close_and_wait());
        CHECK(calls == 2);
        CHECK(destroyed == 1);
        CHECK(exited->load());
        peer.finish();
    }
}

DOCTEST_TEST_CASE("MySQL pool permanent shutdown retires active queued and waiting callbacks once") {
    std::promise<void> sent;
    auto ready = sent.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3active", 7));
        sent.set_value();
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    auto acquired = acquire(pool);
    REQUIRE(bool(acquired));
    connection_result active, queued, waiting;
    std::atomic<unsigned> calls{}, destroyed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    acquired.connection.async_query("active", [&, capture](auto result) {
        active = std::move(result.status);
        ++calls;
    });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    acquired.connection.async_query("queued", [&, capture](auto result) {
        queued = std::move(result.status);
        ++calls;
    });
    pool.async_acquire([&, capture](auto result) {
        waiting = std::move(result.status);
        ++calls;
    });
    capture.reset();
    CHECK(pool.close_and_wait());
    CHECK(calls == 3);
    CHECK(active.code == connection_errc::cancelled);
    CHECK(active.outcome_unknown);
    CHECK(queued.code == connection_errc::cancelled);
    CHECK_FALSE(queued.outcome_unknown);
    CHECK(waiting.code == connection_errc::cancelled);
    CHECK(destroyed == 1);
    CHECK_FALSE(acquired.connection);
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool IO callback cannot wait for its own runtime") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3usable", 7));
        co_await server.send(ok_packet);
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    std::promise<mysql_pool::acquire_result> completion;
    auto ready = completion.get_future();
    pool.async_acquire([&](auto result) {
        CHECK_FALSE(pool.close_and_wait());
        CHECK(arknet::get_last_error() == asio::error::operation_not_supported);
        completion.set_value(std::move(result));
    });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    auto acquired = ready.get();
    REQUIRE(bool(acquired));
    std::promise<query_result> queried;
    auto query_ready = queried.get_future();
    acquired.connection.async_query("usable", [&](auto result) { queried.set_value(std::move(result)); });
    REQUIRE(query_ready.wait_for(5s) == std::future_status::ready);
    CHECK(bool(query_ready.get()));
    CHECK(pool.close_and_wait());
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL retained discarded leases and statement-only handles become inert") {
    for (const bool retain_lease : {false, true})
    {
        mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
            co_await authenticate(server);
            CHECK(co_await receive_command(server) == std::string("\x16"
                                                                  "retained",
                                                          9));
            co_await server.send(prepare_ok(91));
            co_await server.wait_for_eof();
        });
        auto pool = std::make_unique<mysql_pool>(pooled_options(peer));
        auto acquired = acquire(*pool);
        REQUIRE(bool(acquired));
        auto prepared = prepare(acquired.connection, "retained");
        REQUIRE(bool(prepared));
        acquired.connection.discard();
        // The returned entry is removed before the barrier; only the old lease
        // (or statement metadata alone) may still retain its child resources.
        peer.finish();
        if (!retain_lease)
        {
            acquired.connection = {};
        }
        CHECK(pool->close_and_wait());
        pool.reset();
        CHECK_FALSE(prepared.statement.is_open());
        CHECK_FALSE(acquired.connection);
        const auto caller = std::this_thread::get_id();
        unsigned calls{};
        CHECK(acquired.connection.async_execute(prepared.statement, {}, [&](auto result) {
            CHECK(std::this_thread::get_id() == caller);
            CHECK(result.status.code == connection_errc::not_connected);
            ++calls;
        }) == 0);
        CHECK(calls == 1);
        acquired.connection.cancel(1);
        acquired.connection.release();
        acquired.connection.discard();
        std::thread destroy(
            [statement = std::move(prepared.statement), lease = std::move(acquired.connection)]() mutable {
                statement = {};
                lease = {};
            });
        destroy.join();
    }
}

DOCTEST_TEST_CASE("MySQL ordinary pool close followed by permanent shutdown preserves terminal delivery") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    auto acquired = acquire(pool);
    REQUIRE(bool(acquired));
    pool.close();
    std::atomic<unsigned> calls{};
    pool.async_acquire([&](auto result) {
        CHECK(result.status.code == connection_errc::cancelled);
        ++calls;
    });
    acquired.connection.release();
    CHECK(pool.close_and_wait());
    CHECK(calls == 1);
    CHECK_FALSE(acquired.connection);
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL ordinary pool close retires queued waiters before their reentrant submissions") {
    mysql_peer peer([](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    std::promise<mysql_pool::acquire_result> initial_completion;
    auto initial = initial_completion.get_future();
    std::array<std::promise<connection_errc>, 3> completions;
    std::array<std::future<connection_errc>, 3> ready{
        completions[0].get_future(), completions[1].get_future(), completions[2].get_future()};
    std::array<std::atomic<unsigned>, 3> calls{};
    std::array<arknet::mysql::request_id, 2> ids{};
    std::atomic<unsigned> destroyed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    std::promise<void> queued;
    auto fenced = queued.get_future();
    auto retire = [](mysql_pool* owner) noexcept {
        try
        {
            if (!owner->close_and_wait())
            {
                std::terminate();
            }
        }
        catch (...)
        { std::terminate(); }
    };
    std::unique_ptr<mysql_pool, decltype(retire)> retirement(&pool, retire);
    pool.async_acquire([&](auto response) { initial_completion.set_value(std::move(response)); });
    REQUIRE(initial.wait_for(5s) == std::future_status::ready);
    auto acquired = initial.get();
    REQUIRE(bool(acquired));
    for (std::size_t index = 0; index != ids.size(); ++index)
    {
        ids[index] = pool.async_acquire([&, capture, index](auto result) {
            CHECK_FALSE(result.connection);
            ++calls[index];
            if (index == 0)
            {
                pool.close();
                pool.cancel(ids[1]);
                pool.async_acquire([&, capture](auto rejected) {
                    CHECK_FALSE(rejected.connection);
                    ++calls[2];
                    completions[2].set_value(rejected.status.code);
                });
            }
            completions[index].set_value(result.status.code);
        });
    }
    pool.async_acquire(
        [&](auto result) {
            CHECK(result.status.code == connection_errc::invalid_options);
            queued.set_value();
        },
        -1ms);
    REQUIRE(fenced.wait_for(5s) == std::future_status::ready);
    CHECK(ready[0].wait_for(0ms) == std::future_status::timeout);
    CHECK(ready[1].wait_for(0ms) == std::future_status::timeout);
    capture.reset();
    pool.close();
    for (auto& completion : ready)
    {
        REQUIRE(completion.wait_for(5s) == std::future_status::ready);
        CHECK(completion.get() == connection_errc::cancelled);
    }
    CHECK(pool.close_and_wait());
    for (const auto& count : calls)
    {
        CHECK(count == 1);
    }
    CHECK(destroyed == 1);
    CHECK_FALSE(acquired.connection);
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool shutdown waiters observe complete captures and callback-owned facade destruction") {
    std::promise<void> entered;
    auto entered_ready = entered.get_future();
    std::promise<void> resume;
    auto resume_ready = resume.get_future().share();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string("\3pending", 8));
        entered.set_value();
        co_await server.wait_for_eof();
    });
    auto pool = std::make_unique<mysql_pool>(pooled_options(peer));
    auto* caller = pool.get();
    auto acquired = acquire(*pool);
    REQUIRE(bool(acquired));
    std::atomic<unsigned> destroyed{}, calls{}, closed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    acquired.connection.async_query("pending", [&, capture](auto result) {
        CHECK(result.status.code == connection_errc::cancelled);
        resume_ready.wait();
        pool.reset();
        ++calls;
    });
    capture.reset();
    REQUIRE(entered_ready.wait_for(5s) == std::future_status::ready);
    // Only one facade access is allowed to overlap its destruction. Other
    // concurrent-waiter coverage uses a facade kept alive by the test below.
    std::thread waiter([&] {
        if (caller->close_and_wait())
        {
            ++closed;
        }
    });
    resume.set_value();
    waiter.join();
    CHECK(closed == 1);
    CHECK(calls == 1);
    CHECK(destroyed == 1);
    CHECK_FALSE(pool);
    CHECK_FALSE(acquired.connection);
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL concurrent pool publication and permanent waiters complete each acquisition once") {
    mysql_pool pool;
    std::atomic<unsigned> calls{}, closed{};
    std::latch start(1);
    std::vector<std::thread> threads;
    for (unsigned producer = 0; producer != 2; ++producer)
    {
        threads.emplace_back([&] {
            start.wait();
            for (unsigned index = 0; index != 128; ++index)
            {
                const auto id = pool.async_acquire(
                    [&](auto result) {
                        CHECK_FALSE(result.connection);
                        ++calls;
                    },
                    -1ms);
                pool.cancel(id);
            }
        });
    }
    for (unsigned index = 0; index != 3; ++index)
    {
        threads.emplace_back([&] {
            start.wait();
            if (pool.close_and_wait())
            {
                ++closed;
            }
        });
    }
    start.count_down();
    for (auto& thread : threads)
    {
        thread.join();
    }
    CHECK(calls == 256);
    CHECK(closed == 3);
    CHECK(pool.close_and_wait());
}

DOCTEST_TEST_CASE("MySQL discarding one child preserves a second lease on the shared runtime") {
    paired_peer peer;
    pool_options options;
    options.max_connections = 2;
    options.io_threads = 2;
    options.connection.host = "127.0.0.1";
    options.connection.port = peer.port();
    options.connection.username = "test";
    options.connection.password = "password";
    options.connection.database = "test-db";
    options.connection.tls = tls_mode::disabled;
    mysql_pool pool(options);
    auto first = acquire(pool);
    auto second = acquire(pool);
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    first.connection.discard();
    std::promise<query_result> completion;
    auto ready = completion.get_future();
    second.connection.async_query("other lease", [&](auto result) { completion.set_value(std::move(result)); });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    CHECK(bool(ready.get()));
    CHECK(bool(second.connection));
    CHECK(pool.close_and_wait());
    CHECK_FALSE(first.connection);
    CHECK_FALSE(second.connection);
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool shutdown clears a retained lease during unfinished reset") {
    std::promise<void> resetting;
    auto ready = resetting.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        CHECK(co_await receive_command(server) == std::string(1, '\x1f'));
        resetting.set_value();
        co_await server.wait_for_eof();
    });
    auto pool = std::make_unique<mysql_pool>(pooled_options(peer));
    auto acquired = acquire(*pool);
    REQUIRE(bool(acquired));
    acquired.connection.release();
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    CHECK(pool->close_and_wait());
    pool.reset();
    const auto caller = std::this_thread::get_id();
    unsigned calls{};
    auto verify = [&](connection_result result) {
        CHECK(std::this_thread::get_id() == caller);
        CHECK(result.code == connection_errc::not_connected);
        acquired.connection.release();
        ++calls;
    };
    CHECK(acquired.connection.async_query("closed", [&](auto result) { verify(std::move(result.status)); }) == 0);
    CHECK(acquired.connection.async_prepare("closed", [&](auto result) { verify(std::move(result.status)); }) == 0);
    CHECK(acquired.connection.async_execute({}, {}, [&](auto result) { verify(std::move(result.status)); }) == 0);
    CHECK(acquired.connection.async_close({}, verify) == 0);
    CHECK(acquired.connection.async_begin([&](auto result) { verify(std::move(result.status)); }) == 0);
    CHECK(acquired.connection.async_commit([&](auto result) { verify(std::move(result.status)); }) == 0);
    CHECK(acquired.connection.async_rollback([&](auto result) { verify(std::move(result.status)); }) == 0);
    CHECK(calls == 7);
    CHECK_THROWS_AS(
        acquired.connection.async_begin([](auto) { throw std::runtime_error("lease rejection"); }), std::runtime_error);
    acquired.connection = {};
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL pool waits for an executing callback and rejects work after its gate closes") {
    mysql_pool pool;
    std::promise<void> entered;
    auto entered_ready = entered.get_future();
    std::promise<void> resume;
    auto resume_ready = resume.get_future().share();
    std::atomic<unsigned> callbacks{};
    pool.async_acquire(
        [&](auto result) {
            CHECK(result.status.code == connection_errc::invalid_options);
            entered.set_value();
            resume_ready.wait();
            ++callbacks;
        },
        -1ms);
    REQUIRE(entered_ready.wait_for(5s) == std::future_status::ready);
    std::promise<bool> completed;
    auto complete_ready = completed.get_future();
    std::thread waiter([&] { completed.set_value(pool.close_and_wait()); });
    arknet::mysql::request_id id = 1;
    while (id)
    {
        id = pool.async_acquire(
            [&](auto result) {
                CHECK((result.status.code == connection_errc::not_connected ||
                       result.status.code == connection_errc::cancelled ||
                       result.status.code == connection_errc::invalid_options));
            },
            -1ms);
        if (id)
        {
            std::this_thread::sleep_for(1ms);
        }
    }
    CHECK(complete_ready.wait_for(0ms) == std::future_status::timeout);
    resume.set_value();
    waiter.join();
    CHECK(complete_ready.get());
    CHECK(callbacks == 1);
}

DOCTEST_TEST_CASE("MySQL retained lease producers race safely with permanent pool shutdown") {
    std::promise<void> sent;
    auto ready = sent.get_future();
    mysql_peer peer([&](mysql_peer& server) -> asio::awaitable<void> {
        co_await authenticate(server);
        co_await receive_command(server);
        sent.set_value();
        co_await server.wait_for_eof();
    });
    mysql_pool pool(pooled_options(peer));
    auto acquired = acquire(pool);
    REQUIRE(bool(acquired));
    std::atomic<unsigned> calls{};
    acquired.connection.async_query("active", [&](auto result) {
        CHECK(result.status.code == connection_errc::cancelled);
        CHECK(result.status.outcome_unknown);
        ++calls;
    });
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    std::latch start(1);
    std::vector<std::thread> producers;
    for (unsigned producer = 0; producer != 2; ++producer)
    {
        producers.emplace_back([&] {
            start.wait();
            for (unsigned index = 0; index != 128; ++index)
            {
                const auto id = acquired.connection.async_query("queued", [&](auto result) {
                    CHECK((result.status.code == connection_errc::cancelled ||
                           result.status.code == connection_errc::not_connected));
                    CHECK_FALSE(result.status.outcome_unknown);
                    ++calls;
                });
                acquired.connection.cancel(id);
            }
        });
    }
    start.count_down();
    CHECK(pool.close_and_wait());
    for (auto& producer : producers)
    {
        producer.join();
    }
    CHECK(calls == 257);
    peer.finish();
}

DOCTEST_TEST_CASE("MySQL concurrent pool waiters observe callback destruction and worker exit") {
    mysql_pool pool;
    std::atomic<unsigned> destroyed{}, calls{}, closed{};
    auto capture = std::make_shared<destruction_probe>(destroyed);
    auto exited = std::make_shared<std::atomic<bool>>(false);
    std::promise<void> entered, resume;
    auto entered_ready = entered.get_future();
    auto resume_ready = resume.get_future().share();
    pool.async_acquire(
        [&, capture, exited](auto result) {
            CHECK(result.status.code == connection_errc::invalid_options);
            record_worker_exit(exited);
            entered.set_value();
            resume_ready.wait();
            ++calls;
        },
        -1ms);
    capture.reset();
    REQUIRE(entered_ready.wait_for(5s) == std::future_status::ready);
    std::vector<std::thread> waiters;
    for (unsigned index = 0; index != 3; ++index)
    {
        waiters.emplace_back([&] {
            if (pool.close_and_wait())
            {
                CHECK(calls == 1);
                CHECK(destroyed == 1);
                CHECK(exited->load());
                ++closed;
            }
        });
    }
    // Observe the closed publication gate while the callback still owns its
    // capture. The finalizer and its concurrent waiters must all remain pending.
    while (pool.async_acquire([](auto) {}, -1ms))
    {
        std::this_thread::sleep_for(1ms);
    }
    CHECK(closed == 0);
    resume.set_value();
    for (auto& waiter : waiters)
    {
        waiter.join();
    }
    CHECK(closed == 3);
}
