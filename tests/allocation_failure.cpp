// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#if defined(_MSC_VER)
#include "scoped_asio_allocation_failure.hpp"

#endif

#include <chrono>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <thread>

#include <arknet/external/asio.hpp>
#include <arknet/rudp/detail/transport.hpp>
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

DOCTEST_TEST_CASE("RUDP failed packet allocation preserves byte accounting and send capacity") {
    asio::io_context context;
    asio::ip::udp::socket receiver(context, {asio::ip::udp::v4(), 0});
    receiver.non_blocking(true);
    auto socket = std::make_shared<asio::ip::udp::socket>(context, asio::ip::udp::v4());
    arknet::arq::options options;
    options.max_pending_bytes = 64;
    unsigned failures = 0;
    auto channel = std::make_shared<arknet::detail::rudp_channel>(
        socket, asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), receiver.local_endpoint().port()), false,
        1, options, [](std::string_view) {}, [&](arknet::error_code) { ++failures; });
    const std::string payload(64, 'r');
    const auto before = scoped_allocation_failure::injections.load();
    bool threw = false;
    {
        scoped_allocation_failure::scope failure;
        try
        { channel->raw_send(payload); }
        catch (const std::bad_alloc&)
        { threw = true; }
    }
    CHECK(threw);
    CHECK(scoped_allocation_failure::injections == before + 1);
    CHECK(channel->statistics().raw_queued_bytes == 0);
    CHECK(channel->statistics().wire_bytes_tx == 0);
    CHECK(failures == 0);

    // Use the entire configured budget: a phantom reservation would reject it.
    channel->raw_send(payload);
    CHECK(channel->statistics().raw_queued_bytes == payload.size());
    CHECK(channel->statistics().wire_bytes_tx == payload.size());
    context.run();
    CHECK(channel->statistics().raw_queued_bytes == 0);
    CHECK(failures == 0);
    std::string received(payload.size(), '\0');
    asio::ip::udp::endpoint sender;
    arknet::error_code error;
    std::size_t count = 0;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    // Local send completion can precede receive readiness, even on loopback.
    do
    {
        count = receiver.receive_from(asio::buffer(received), sender, 0, error);
        if (error != asio::error::would_block && error != asio::error::try_again)
        {
            break;
        }
        std::this_thread::sleep_for(1ms);
    } while (std::chrono::steady_clock::now() < deadline);
    CHECK(count == payload.size());
    CHECK_FALSE(error);
    CHECK(received == payload);
    channel->stop();
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
