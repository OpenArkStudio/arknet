// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <algorithm>
#include <array>
#include <memory>
#include <memory_resource>
#include <optional>
#include <sstream>
#include <string>

#include "vendor/doctest/doctest.h"

#include "../benchmarks/connections_socket_trace.hpp"

namespace {

namespace asio = connection_load::socket_trace_asio;
using error_code = connection_load::socket_trace_error;
using connection_load::operation_ring;
using connection_load::socket_trace_clock;

// Deterministic AsyncReadStream/AsyncWriteStream: no sockets, workers or sleeps.
struct fake_stream
{
    using executor_type = asio::io_context::executor_type;
    asio::io_context& context;
    std::string input, output;
    std::size_t chunk = 2, reads{}, writes{};
    error_code error;
    std::optional<asio::any_io_executor> expected_executor, expected_immediate_executor;
    std::pmr::memory_resource* expected_allocator{};
    asio::cancellation_slot expected_slot;

    executor_type get_executor() noexcept {
        return context.get_executor();
    }

    template<class Buffers, class Handler>
    void submit(const Buffers& buffers, Handler handler, bool read) {
        const auto executor = asio::get_associated_executor(handler, get_executor());
        const auto allocator = asio::get_associated_allocator(handler);
        auto slot = asio::get_associated_cancellation_slot(handler);
        if (expected_executor)
        {
            DOCTEST_CHECK(asio::any_io_executor(executor) == *expected_executor);
        }
        if (expected_immediate_executor)
        {
            DOCTEST_CHECK(asio::any_io_executor(asio::get_associated_immediate_executor(handler, get_executor())) ==
                          *expected_immediate_executor);
        }
        if (expected_allocator)
        {
            if constexpr (requires { allocator.resource(); })
            {
                DOCTEST_CHECK(allocator.resource() == expected_allocator);
            }
            else
            {
                DOCTEST_FAIL("associated allocator was lost");
            }
        }
        if (expected_slot.is_connected())
        {
            DOCTEST_CHECK(slot == expected_slot);
        }
        auto cancelled = std::make_shared<bool>(false);
        if (slot.is_connected())
        {
            slot.assign([cancelled](asio::cancellation_type) { *cancelled = true; });
        }
        asio::post(executor,
            asio::bind_allocator(allocator, [this, buffers, handler = std::move(handler), cancelled, read]() mutable {
                if (*cancelled)
                {
                    std::move(handler)(asio::error::operation_aborted, 0);
                    return;
                }
                const auto size = (std::min)(chunk, asio::buffer_size(buffers));
                if constexpr (asio::is_mutable_buffer_sequence<Buffers>::value)
                {
                    DOCTEST_REQUIRE(read);
                    const auto received = (std::min)(size, input.size());
                    asio::buffer_copy(buffers, asio::buffer(input), received);
                    input.erase(0, received);
                    std::move(handler)(error, received);
                }
                else
                {
                    DOCTEST_REQUIRE_FALSE(read);
                    std::string bytes(size, '\0');
                    asio::buffer_copy(asio::buffer(bytes), buffers, size);
                    output += bytes;
                    std::move(handler)(error, size);
                }
            }));
    }

    template<class Buffers, class Handler>
    void async_read_some(const Buffers& buffers, Handler handler) {
        ++reads;
        submit(buffers, std::move(handler), true);
    }

    template<class Buffers, class Handler>
    void async_write_some(const Buffers& buffers, Handler handler) {
        ++writes;
        submit(buffers, std::move(handler), false);
    }
};

std::string json(const operation_ring& ring) {
    std::ostringstream output;
    ring.write_json(output);
    return output.str();
}

} // namespace

DOCTEST_TEST_CASE("Socket completion observation preserves associations, cancellation and initiation sequence") {
    asio::io_context context;
    const auto strand = asio::make_strand(context);
    asio::cancellation_signal cancellation;
    std::pmr::unsynchronized_pool_resource memory;
    std::pmr::polymorphic_allocator<std::byte> allocator(&memory);
    fake_stream stream{context};
    stream.expected_executor = strand;
    stream.expected_immediate_executor = strand;
    stream.expected_allocator = &memory;
    stream.expected_slot = cancellation.slot();
    operation_ring ring;
    connection_load::socket_trace_stream observed(stream, ring, socket_trace_clock::now());
    std::array<char, 8> bytes{};
    unsigned calls{};
    observed.read_sequence(42);
    observed.async_read_some(asio::buffer(bytes),
        asio::bind_executor(strand,
            asio::bind_immediate_executor(strand,
                asio::bind_allocator(allocator,
                    asio::bind_cancellation_slot(cancellation.slot(),
                        [owned = std::make_unique<int>(7), &calls, &ring, &strand](error_code error, std::size_t size) {
                            DOCTEST_CHECK(*owned == 7);
                            DOCTEST_CHECK(strand.running_in_this_thread());
                            DOCTEST_CHECK(error == asio::error::operation_aborted);
                            DOCTEST_CHECK(size == 0);
                            DOCTEST_REQUIRE(ring.size() == 2);
                            DOCTEST_CHECK(ring.at(1).error == error.value());
                            ++calls;
                        })))));
    observed.read_sequence(99);
    DOCTEST_CHECK(calls == 0);
    cancellation.emit(asio::cancellation_type::all);
    context.run();
    DOCTEST_CHECK(calls == 1);
    DOCTEST_CHECK(ring.at(0).sequence == 42);
    DOCTEST_CHECK(ring.at(1).sequence == 42);
    DOCTEST_CHECK(ring.at(0).operation_id == ring.at(1).operation_id);
}

DOCTEST_TEST_CASE("Socket trace distinguishes a partial buffer-sequence write from one composed write") {
    asio::io_context context;
    fake_stream stream{context};
    operation_ring ring;
    connection_load::socket_trace_stream observed(stream, ring, socket_trace_clock::now());
    observed.write_sequence(9);
    const std::array<asio::const_buffer, 2> buffers{asio::buffer("ab", 2), asio::buffer("cdef", 4)};
    unsigned completions{};
    asio::async_write(observed, buffers, [&](error_code error, std::size_t bytes) {
        DOCTEST_CHECK_FALSE(error);
        DOCTEST_CHECK(bytes == 6);
        ++completions;
    });
    context.run();
    DOCTEST_CHECK(completions == 1);
    DOCTEST_CHECK(stream.writes == 3);
    DOCTEST_CHECK(stream.output == "abcdef");
    DOCTEST_REQUIRE(ring.size() == 6);
    for (std::size_t index = 0; index < ring.size(); index += 2)
    {
        DOCTEST_CHECK(ring.at(index).bytes == 6 - index);
        DOCTEST_CHECK(ring.at(index + 1).bytes == 2);
        DOCTEST_CHECK(ring.at(index).sequence == 9);
        DOCTEST_CHECK(ring.at(index).operation_id == ring.at(index + 1).operation_id);
    }
    DOCTEST_CHECK(json(ring).find("\"completed_write_bytes\":6") != std::string::npos);
}

DOCTEST_TEST_CASE("Socket trace distinguishes buffered composed frames from underlying read completions") {
    asio::io_context context;
    fake_stream stream{context};
    stream.input = "a\nb\n";
    stream.chunk = 8;
    operation_ring ring;
    connection_load::socket_trace_stream observed(stream, ring, socket_trace_clock::now());
    asio::streambuf input(16);
    unsigned completions{};
    observed.read_sequence(1);
    asio::async_read_until(observed, input, '\n', [&](error_code error, std::size_t bytes) {
        DOCTEST_CHECK_FALSE(error);
        DOCTEST_CHECK(bytes == 2);
        input.consume(bytes);
        ++completions;
        observed.read_sequence(2);
        asio::async_read_until(observed, input, '\n', [&](error_code second_error, std::size_t second_bytes) {
            DOCTEST_CHECK_FALSE(second_error);
            DOCTEST_CHECK(second_bytes == 2);
            ++completions;
        });
    });
    context.run();
    DOCTEST_CHECK(completions == 2);
    DOCTEST_CHECK(stream.reads == 2);
    DOCTEST_REQUIRE(ring.size() == 4);
    DOCTEST_CHECK(ring.at(1).bytes == 4);
    DOCTEST_CHECK(ring.at(1).sequence == 1);
    DOCTEST_CHECK(ring.at(2).bytes == 0);
    DOCTEST_CHECK(ring.at(3).bytes == 0);
    DOCTEST_CHECK(ring.at(3).sequence == 2);
}

DOCTEST_TEST_CASE("Frozen socket history survives actual handler completion without suppressing it") {
    asio::io_context context;
    fake_stream stream{context};
    stream.input = "x";
    stream.error = asio::error::eof;
    operation_ring ring;
    connection_load::socket_trace_stream observed(stream, ring, socket_trace_clock::now());
    std::array<char, 8> bytes{};
    unsigned calls{};
    observed.async_read_some(asio::buffer(bytes), [&](error_code error, std::size_t size) {
        DOCTEST_CHECK(error == asio::error::eof);
        DOCTEST_CHECK(size == 1);
        ++calls;
    });
    ring.freeze();
    const auto before = json(ring);
    context.run();
    DOCTEST_CHECK(calls == 1);
    DOCTEST_CHECK(bytes.front() == 'x');
    DOCTEST_CHECK(json(ring) == before);
}
