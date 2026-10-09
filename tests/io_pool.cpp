#include <arknet/base/io_pool.hpp>
#include "check.hpp"
#include "../benchmarks/runtime.hpp"
#include <atomic>
#include <latch>
#include <thread>

DOCTEST_TEST_CASE("Queued IO cancellation does not retain an unrun or stopped context")
{
    for (const bool serialized : {false, true})
        for (const bool stopped : {false, true})
        {
            DOCTEST_INFO("serialized=", serialized, ", stopped=", stopped);
            std::weak_ptr<asio::io_context> released;
            {
                auto context = std::make_shared<asio::io_context>();
                released = context;
                arknet::io_t lane(context, serialized);
                if (stopped)
                    context->stop();
                lane.cancel();
            }
            DOCTEST_CHECK_MESSAGE(released.expired(), "a queued cancellation must not form an ownership cycle");
        }
}

DOCTEST_TEST_CASE("External IO lanes serialize handlers across multiple context runners by default")
{
    auto context = std::make_shared<asio::io_context>();
    arknet::io_t lane(context);
    std::atomic<unsigned> active{}, maximum{}, completed{};
    for (unsigned index = 0; index < 100; ++index)
        asio::post(lane.executor(),
                   [&]
                   {
                       const auto concurrent = active.fetch_add(1) + 1;
                       auto previous = maximum.load();
                       while (previous < concurrent && !maximum.compare_exchange_weak(previous, concurrent))
                       {
                       }
                       std::this_thread::sleep_for(100us);
                       --active;
                       ++completed;
                   });
    std::vector<std::thread> runners;
    for (unsigned index = 0; index < 4; ++index)
        runners.emplace_back([&] { context->run(); });
    for (auto& runner : runners)
        runner.join();
    check(completed == 100 && maximum == 1, "one external lane never overlaps its handlers");
}

DOCTEST_TEST_CASE("IO pool returns task results and propagates exceptions without losing its worker")
{
    arknet::io_pool pool(2);
    check(pool.start(), "pool starts");
    auto result = pool.post(0, [&] { return pool.running_in_thread(0) ? 7 : -1; });
    check(await(result) == 7, "indexed post selects the requested worker");
    auto failed = pool.post([]() -> int { throw std::runtime_error("task failure"); });
    bool propagated = false;
    try
    {
        await(failed);
    }
    catch (const std::runtime_error& error)
    {
        propagated = std::string_view(error.what()) == "task failure";
    }
    check(propagated, "future preserves the exception");
    auto continued = pool.post(1, [] { return 11; });
    check(await(continued) == 11, "worker remains available after a failed task");
    pool.stop();
    check(pool.stopped(), "pool joins workers");
}

DOCTEST_TEST_CASE("IO pool drains nested tasks and supports restarting")
{
    arknet::io_pool pool(1);
    check(pool.start(), "pool starts");
    std::atomic<unsigned> completed{};
    auto outer = pool.post(
        [&] { asio::post(pool.get_context(0), [&] { asio::post(pool.get_context(0), [&] { ++completed; }); }); });
    await(outer);
    pool.stop();
    check(completed == 1 && pool.get(0)->pending() == 0, "nested work finishes before stop returns");
    check(pool.start(), "pool restarts after joining");
    auto restarted = pool.post([] { return 19; });
    check(await(restarted) == 19, "restarted context executes new work");
    pool.stop();
}

DOCTEST_TEST_CASE("IO pool tolerates concurrent owner stops and rejects submissions after stop")
{
    arknet::io_pool pool(2);
    check(pool.start(), "pool starts");
    std::atomic<unsigned> completed{};
    std::vector<std::future<void>> tasks;
    for (unsigned index = 0; index < 100; ++index)
        tasks.push_back(pool.post([&] { ++completed; }));
    std::latch start(1);
    std::thread first(
        [&]
        {
            start.wait();
            pool.stop();
        });
    std::thread second(
        [&]
        {
            start.wait();
            pool.stop();
        });
    start.count_down();
    first.join();
    second.join();
    for (auto& task : tasks)
        await(task);
    check(completed == 100 && pool.stopped(), "all admitted work completes exactly once");
    auto rejected = pool.post([] { return 1; });
    bool aborted = false;
    try
    {
        await(rejected);
    }
    catch (const arknet::system_error& error)
    {
        aborted = error.code() == asio::error::operation_aborted;
    }
    check(aborted, "submission to a stopped pool completes with an error");
}

DOCTEST_TEST_CASE("IO pool worker stop requests remain joinable by the owner")
{
    arknet::io_pool pool(1);
    check(pool.start(), "pool starts");
    auto request = pool.post(
        [&]
        {
            pool.stop();
            return 23;
        });
    check(await(request) == 23, "stop on a worker does not block the worker");
    pool.stop();
    check(pool.stopped(), "owner completes the stop");
}

DOCTEST_TEST_CASE("Benchmark runtime drains callbacks holding strands from another context")
{
    std::atomic<unsigned> completed{};
    {
        benchmark_runtime runtime("sharded", 4);
        std::latch entered(1), release(1);
        asio::post(runtime.context_at(),
                   [&]
                   {
                       entered.count_down();
                       release.wait();
                   });
        entered.wait();
        asio::post(runtime.context_at(),
                   [strand = asio::make_strand(runtime.context_at(3)), &completed] { ++completed; });
        std::jthread unblock(
            [&]
            {
                std::this_thread::sleep_for(20ms);
                release.count_down();
            });
        runtime.stop();
        DOCTEST_CHECK_MESSAGE(completed == 1, "cross-context captures are released while every context is alive");
        runtime.stop();
    }
    check(completed == 1, "runtime stop remains idempotent");
}
