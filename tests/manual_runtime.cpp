#include <atomic>
#include <chrono>
#include <exception>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include <arknet/base/manual_runtime.hpp>
#include "check.hpp"

namespace {

struct capture_lifetime
{
    std::atomic<unsigned>& destroyed;
    std::thread::id& destruction_thread;

    ~capture_lifetime() {
        destruction_thread = std::this_thread::get_id();
        ++destroyed;
    }
};

asio::awaitable<void> timer_coroutine(std::vector<std::thread::id>& threads) {
    threads.push_back(std::this_thread::get_id());
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::steady_clock::duration::zero());
    co_await timer.async_wait(asio::use_awaitable);
    threads.push_back(std::this_thread::get_id());
}

asio::awaitable<void> throwing_coroutine() {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::steady_clock::duration::zero());
    co_await timer.async_wait(asio::use_awaitable);
    throw std::runtime_error("manual coroutine failure");
}

} // namespace

DOCTEST_TEST_CASE("Manual runtime posts from other threads but runs only when its owner polls") {
    static_assert(!std::is_copy_constructible_v<arknet::manual_runtime>);
    static_assert(!std::is_move_constructible_v<arknet::manual_runtime>);
    arknet::manual_runtime runtime;
    const auto owner = std::this_thread::get_id();
    const auto executor = runtime.get_executor();
    std::atomic<unsigned> calls{};
    std::vector<std::thread::id> threads;
    std::thread producer([&] {
        for (unsigned index = 0; index < 8; ++index)
        {
            asio::post(executor, [&] {
                threads.push_back(std::this_thread::get_id());
                ++calls;
            });
        }
    });
    producer.join();
    DOCTEST_CHECK(calls == 0);
    DOCTEST_CHECK(runtime.poll(8) == 8);
    DOCTEST_CHECK(calls == 8);
    DOCTEST_REQUIRE(threads.size() == 8);
    for (const auto thread : threads)
    {
        DOCTEST_CHECK(thread == owner);
    }
    DOCTEST_CHECK(runtime.poll() == 0);
}

DOCTEST_TEST_CASE("Manual runtime remains usable after idle polls and honors its handler budget") {
    arknet::manual_runtime runtime;
    DOCTEST_CHECK(runtime.poll() == 0);
    DOCTEST_CHECK(runtime.poll() == 0);
    unsigned calls{};
    for (unsigned index = 0; index < 5; ++index)
    {
        asio::post(runtime.get_executor(), [&] { ++calls; });
    }
    DOCTEST_CHECK(runtime.poll(0) == 0);
    DOCTEST_CHECK(calls == 0);
    DOCTEST_CHECK(runtime.poll(2) == 2);
    DOCTEST_CHECK(calls == 2);
    DOCTEST_CHECK(runtime.poll(2) == 2);
    DOCTEST_CHECK(calls == 4);
    DOCTEST_CHECK(runtime.poll(2) == 1);
    DOCTEST_CHECK(calls == 5);
    DOCTEST_CHECK(runtime.poll() == 0);
    asio::post(runtime.get_executor(), [&] { ++calls; });
    DOCTEST_CHECK(runtime.poll(1) == 1);
    DOCTEST_CHECK(calls == 6);
}

DOCTEST_TEST_CASE("Manual runtime accepts a cross-thread post while its owner is polling") {
    arknet::manual_runtime runtime;
    const auto owner = std::this_thread::get_id();
    const auto executor = runtime.get_executor();
    std::latch entered(1), posted(1);
    std::thread::id execution_thread;
    std::thread producer([&] {
        entered.wait();
        asio::post(executor, [&] { execution_thread = std::this_thread::get_id(); });
        posted.count_down();
    });
    asio::post(executor, [&] {
        entered.count_down();
        posted.wait();
    });
    const auto first = runtime.poll(1);
    producer.join();
    DOCTEST_CHECK(first == 1);
    DOCTEST_CHECK(execution_thread == std::thread::id{});
    DOCTEST_CHECK(runtime.poll(1) == 1);
    DOCTEST_CHECK(execution_thread == owner);
}

DOCTEST_TEST_CASE("Manual runtime budgets handlers posted by another handler") {
    arknet::manual_runtime runtime;
    unsigned calls{};
    asio::post(runtime.get_executor(), [&] {
        ++calls;
        asio::post(runtime.get_executor(), [&] { ++calls; });
    });
    DOCTEST_CHECK(runtime.poll(1) == 1);
    DOCTEST_CHECK(calls == 1);
    DOCTEST_CHECK(runtime.poll(1) == 1);
    DOCTEST_CHECK(calls == 2);
}

DOCTEST_TEST_CASE("Manual runtime rejects foreign-thread polling without executing work") {
    arknet::manual_runtime runtime;
    std::atomic<unsigned> calls{};
    asio::post(runtime.get_executor(), [&] { ++calls; });
    bool rejected{};
    std::thread foreign([&] {
        try
        { runtime.poll(); }
        catch (const std::logic_error&)
        { rejected = true; }
    });
    foreign.join();
    DOCTEST_CHECK(rejected);
    DOCTEST_CHECK(calls == 0);
    DOCTEST_CHECK(runtime.poll() == 1);
    DOCTEST_CHECK(calls == 1);
}

DOCTEST_TEST_CASE("Manual runtime rejects reentrant polling and recovers after the exception") {
    arknet::manual_runtime runtime;
    asio::post(runtime.get_executor(), [&] { runtime.poll(); });
    DOCTEST_CHECK_THROWS_AS(runtime.poll(), std::logic_error);
    unsigned calls{};
    asio::post(runtime.get_executor(), [&] { ++calls; });
    DOCTEST_CHECK(runtime.poll() == 1);
    DOCTEST_CHECK(calls == 1);
}

DOCTEST_TEST_CASE("Manual runtime preserves queued work after a handler throws") {
    arknet::manual_runtime runtime;
    unsigned calls{};
    asio::post(runtime.get_executor(), [] { throw std::runtime_error("manual handler failure"); });
    asio::post(runtime.get_executor(), [&] { ++calls; });
    DOCTEST_CHECK_THROWS_AS(runtime.poll(), std::runtime_error);
    DOCTEST_CHECK(calls == 0);
    DOCTEST_CHECK(runtime.poll() == 1);
    DOCTEST_CHECK(calls == 1);
    DOCTEST_CHECK(runtime.poll() == 0);
}

DOCTEST_TEST_CASE("Manual runtime resumes timer coroutines and their completion on the owner thread") {
    arknet::manual_runtime runtime;
    const auto owner = std::this_thread::get_id();
    std::vector<std::thread::id> threads;
    std::exception_ptr failure;
    bool completed{};
    asio::co_spawn(runtime.get_executor(), timer_coroutine(threads), [&](std::exception_ptr error) {
        threads.push_back(std::this_thread::get_id());
        failure = std::move(error);
        completed = true;
    });
    DOCTEST_CHECK(threads.empty());
    DOCTEST_CHECK_FALSE(completed);
    for (unsigned attempt = 0; attempt < 1024 && !completed; ++attempt)
    {
        DOCTEST_CHECK(runtime.poll(1) <= 1);
    }
    DOCTEST_REQUIRE(completed);
    DOCTEST_CHECK_FALSE(bool(failure));
    DOCTEST_REQUIRE(threads.size() == 3);
    for (const auto thread : threads)
    {
        DOCTEST_CHECK(thread == owner);
    }
}

DOCTEST_TEST_CASE("Manual coroutine exceptions arrive at co_spawn completion and do not stop polling") {
    arknet::manual_runtime runtime;
    std::exception_ptr failure;
    bool completed{};
    asio::co_spawn(runtime.get_executor(), throwing_coroutine(), [&](std::exception_ptr error) {
        failure = std::move(error);
        completed = true;
    });
    for (unsigned attempt = 0; attempt < 1024 && !completed; ++attempt)
    {
        runtime.poll(1);
    }
    DOCTEST_REQUIRE(completed);
    DOCTEST_REQUIRE(bool(failure));
    DOCTEST_CHECK_THROWS_AS(std::rethrow_exception(failure), std::runtime_error);
    unsigned calls{};
    asio::post(runtime.get_executor(), [&] { ++calls; });
    DOCTEST_CHECK(runtime.poll() == 1);
    DOCTEST_CHECK(calls == 1);
}

DOCTEST_TEST_CASE("Manual runtime destruction releases unexecuted captures on its calling thread") {
    const auto owner = std::this_thread::get_id();
    std::atomic<unsigned> destroyed{};
    std::thread::id destruction_thread;
    unsigned calls{};
    {
        arknet::manual_runtime runtime;
        auto capture = std::make_shared<capture_lifetime>(destroyed, destruction_thread);
        asio::post(runtime.get_executor(), [capture, &calls] { ++calls; });
        capture.reset();
        DOCTEST_CHECK(destroyed == 0);
    }
    DOCTEST_CHECK(calls == 0);
    DOCTEST_CHECK(destroyed == 1);
    DOCTEST_CHECK(destruction_thread == owner);
}

DOCTEST_TEST_CASE("Manual runtime polling stays alive when a handler releases its facade") {
    auto runtime = std::make_unique<arknet::manual_runtime>();
    asio::post(runtime->get_executor(), [&] { runtime.reset(); });
    auto* active = runtime.get();
    DOCTEST_CHECK(active->poll(1) == 1);
    DOCTEST_CHECK_FALSE(bool(runtime));
}
