// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#define ARKNET_COROUTINE_BENCHMARK_NO_MAIN
#include "../benchmarks/coroutine.cpp"
#include "check.hpp"

DOCTEST_TEST_CASE("Native TCP benchmark drains both execution modes after an exception before traffic")
{
    for (const auto* execution : {"callback", "coroutine"})
        for (const auto* model : {"shared", "sharded"})
        {
            options settings;
            settings.model = model;
            settings.execution = execution;
            settings.threads = 4;
            settings.clients = 16;
            const auto started = clock_type::now();
            bool propagated = false;
            try
            {
                run(settings, [] { throw std::runtime_error("injected failure after connect"); });
            }
            catch (const std::runtime_error& error)
            {
                propagated = std::string_view(error.what()) == "injected failure after connect";
            }
            check(propagated, "injected failure reaches the caller after listener and socket cancellation");
            check(clock_type::now() - started < 5s, "waiting client gates and server reads drain promptly");
        }
}

DOCTEST_TEST_CASE("Native callback and coroutine clients validate complete TCP batches")
{
    for (const auto* execution : {"callback", "coroutine"})
        for (const auto* model : {"shared", "sharded"})
            for (const auto window : {1u, 64u})
            {
                options settings;
                settings.model = model;
                settings.execution = execution;
                settings.threads = 4;
                settings.clients = 4;
                settings.window = window;
                settings.payload = 64;
                settings.warmup = .05;
                settings.seconds = .1;
                DOCTEST_CHECK_NOTHROW(run(settings));
            }
}

DOCTEST_TEST_CASE("Native TCP maximum batches complete without opposing write stalls")
{
    for (const auto* execution : {"callback", "coroutine"})
        for (const auto* model : {"shared", "sharded"})
        {
            options settings;
            settings.model = model;
            settings.execution = execution;
            settings.threads = 4;
            settings.clients = 2;
            settings.payload = 65507;
            settings.window = 64;
            settings.warmup = 0;
            settings.seconds = .2;
            DOCTEST_CHECK_NOTHROW(run(settings));
        }
}
