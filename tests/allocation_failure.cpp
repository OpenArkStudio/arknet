// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#if defined(_MSC_VER)
#include "scoped_asio_allocation_failure.hpp"
#endif

#include <cstdint>
#include <new>

#include <arknet/external/asio.hpp>
#include "check.hpp"
#include "scoped_allocation_failure.hpp"

DOCTEST_TEST_CASE("Allocation failure is one-shot and normal aligned allocation recovers") {
    const auto before = scoped_allocation_failure::injections.load();
    bool threw = false, disarmed = false, recovered = false;
    {
        scoped_allocation_failure::scope failure;
        try
        { ::operator delete(::operator new(128)); }
        catch (const std::bad_alloc&)
        { threw = true; }
        disarmed = !scoped_allocation_failure::armed;
        auto* pointer = ::operator new(128, std::nothrow);
        recovered = pointer != nullptr;
        ::operator delete(pointer);
    }
    CHECK(threw);
    CHECK(disarmed);
    CHECK(recovered);
    CHECK(scoped_allocation_failure::injections == before + 1);
    auto* aligned = ::operator new(257, std::align_val_t{64});
    CHECK(reinterpret_cast<std::uintptr_t>(aligned) % 64 == 0);
    ::operator delete(aligned, std::align_val_t{64});
}

DOCTEST_TEST_CASE("Asio post allocation failure leaves no handler and permits normal post recovery") {
    asio::io_context context;
    asio::post(context, [] {});
    context.run();
    context.restart();
    unsigned invoked = 0;
    const auto before = scoped_allocation_failure::injections.load();
    bool threw = false;
    {
        scoped_allocation_failure::scope failure;
        try
        {
            asio::post(context, [&] { ++invoked; });
        }
        catch (const std::bad_alloc&)
        { threw = true; }
    }
    CHECK(threw);
    CHECK(scoped_allocation_failure::injections == before + 1);
    CHECK_FALSE(scoped_allocation_failure::armed);
    CHECK(context.poll() == 0);
    CHECK(invoked == 0);
    context.restart();
    asio::post(context, [&] { ++invoked; });
    CHECK(context.run() == 1);
    CHECK(invoked == 1);
}

#if defined(_MSC_VER)
DOCTEST_TEST_CASE("MSVC Asio CRT allocation preserves alignment and throws through Asio on failure") {
    const auto before = scoped_allocation_failure::injections.load();
    bool threw = false;
    {
        scoped_allocation_failure::scope failure;
        try
        { asio::aligned_delete(asio::aligned_new(64, 257)); }
        catch (const std::bad_alloc&)
        { threw = true; }
    }
    CHECK(threw);
    CHECK(scoped_allocation_failure::injections == before + 1);
    CHECK_FALSE(scoped_allocation_failure::armed);
    auto* aligned = asio::aligned_new(64, 257);
    CHECK(reinterpret_cast<std::uintptr_t>(aligned) % 64 == 0);
    asio::aligned_delete(aligned);
}
#endif
