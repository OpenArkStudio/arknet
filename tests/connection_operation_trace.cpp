// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <limits>
#include <sstream>
#include <string>

#include "vendor/doctest/doctest.h"

#include "../benchmarks/connections_trace.hpp"

namespace {

using namespace connection_load;

template<class Value>
std::string json(const Value& value) {
    std::ostringstream out;
    value.write_json(out);
    return out.str();
}
} // namespace

DOCTEST_TEST_CASE("Disabled operation recording allocates no session slots or worker probes") {
    operation_recorder recorder(false, (std::numeric_limits<std::size_t>::max)());
    DOCTEST_CHECK_FALSE(recorder.enabled());
    DOCTEST_CHECK(recorder.slots() == 0);
    DOCTEST_CHECK(recorder.acquire() == nullptr);
    DOCTEST_CHECK_FALSE(recorder.request_marker());
    const auto before = json(recorder);
    recorder.complete_marker(1, 2);
    DOCTEST_CHECK(json(recorder) == before);
    DOCTEST_CHECK(recorder.storage_bytes() == sizeof(operation_recorder));
}

DOCTEST_TEST_CASE("Composed operation history pairs concurrent directions and freezes before cancellation") {
    operation_ring ring;
    ring.record(operation_phase::read_start, 10, 0, 39);
    ring.record(operation_phase::write_start, 11, 1091, 42);
    ring.record(operation_phase::write_complete, 12, 1091, 42);
    DOCTEST_REQUIRE(ring.size() == 3);
    DOCTEST_CHECK(ring.at(0).operation_id == 1);
    DOCTEST_CHECK(ring.at(1).operation_id == 2);
    DOCTEST_CHECK(ring.at(2).operation_id == 2);
    DOCTEST_CHECK(ring.history_complete());
    ring.freeze();
    const auto before = json(ring);
    ring.record(operation_phase::read_complete, 20, 0, 39, 125);
    DOCTEST_CHECK(json(ring) == before);
    DOCTEST_CHECK(before.find("\"pending_read_id\":1") != std::string::npos);
    DOCTEST_CHECK(before.find("\"pending_write_id\":0") != std::string::npos);
    DOCTEST_CHECK(before.find("\"completed_write_bytes\":1091") != std::string::npos);
    DOCTEST_CHECK(before.find("\"frozen\":true") != std::string::npos);
}

DOCTEST_TEST_CASE("Ring wrap retains chronological tail and discloses missing history and invalid transitions") {
    operation_ring ring;
    for (int index = 0; index < 12; ++index)
    {
        ring.record(operation_phase::write_start, 2 * index, 1091, index + 1);
        ring.record(operation_phase::write_complete, 2 * index + 1, 1091, index + 1, index == 11 ? 5 : 0);
    }
    DOCTEST_REQUIRE(ring.size() == 16);
    DOCTEST_CHECK(ring.overwritten() == 8);
    DOCTEST_CHECK_FALSE(ring.history_complete());
    DOCTEST_CHECK(ring.at(0).ordinal == 9);
    DOCTEST_CHECK(ring.at(15).ordinal == 24);
    DOCTEST_CHECK(ring.at(15).error == 5);
    const auto output = json(ring);
    DOCTEST_CHECK(output.find("\"completed_write_bytes\":13092") != std::string::npos);
    DOCTEST_CHECK(output.find("\"history_complete\":false") != std::string::npos);
    operation_ring invalid;
    invalid.record(operation_phase::read_complete, 1, 1, 1);
    DOCTEST_CHECK(invalid.invalid_transitions() == 1);
    DOCTEST_CHECK_FALSE(invalid.history_complete());
}

DOCTEST_TEST_CASE("Trace storage admission is capped independently of declared connection count") {
    operation_recorder recorder(true, (std::numeric_limits<std::size_t>::max)());
    DOCTEST_CHECK(recorder.slots() == operation_recorder::maximum_slots());
    DOCTEST_CHECK(recorder.storage_bytes() + trace_output::limit + operation_recorder::scratch_reservation <=
                  operation_recorder::memory_limit);
    for (std::size_t index = 0; index < recorder.slots(); ++index)
    {
        DOCTEST_REQUIRE(recorder.acquire() != nullptr);
    }
    DOCTEST_CHECK(recorder.acquire() == nullptr);
    DOCTEST_CHECK(recorder.unrecorded_sessions() == 1);
    recorder.freeze();
    DOCTEST_CHECK(recorder.acquire() == nullptr);
    DOCTEST_CHECK(recorder.unrecorded_sessions() == 1);
    DOCTEST_CHECK(json(recorder).find("\"low_level_operations\":false") != std::string::npos);
}

DOCTEST_TEST_CASE("Worker marker admission bounds queued work and retains the frozen observation") {
    operation_recorder recorder(true, 1);
    DOCTEST_CHECK(recorder.request_marker());
    for (int index = 0; index < 100; ++index)
    {
        DOCTEST_CHECK_FALSE(recorder.request_marker());
    }
    recorder.complete_marker(10, 5000);
    for (int index = 0; index < 20; ++index)
    {
        DOCTEST_REQUIRE(recorder.request_marker());
        recorder.complete_marker(6000 + index, 7000 + index);
    }
    DOCTEST_REQUIRE(recorder.request_marker());
    recorder.freeze();
    const auto before = json(recorder);
    recorder.complete_marker(9000, 10000);
    DOCTEST_CHECK(json(recorder) == before);
    DOCTEST_CHECK(before.find("\"worker_markers_submitted\":22") != std::string::npos);
    DOCTEST_CHECK(before.find("\"worker_markers_skipped\":100") != std::string::npos);
    DOCTEST_CHECK(before.find("\"worker_marker_pending\":true") != std::string::npos);
    DOCTEST_CHECK(before.find("\"worker_overwritten_markers\":5") != std::string::npos);
    DOCTEST_CHECK(before.find("\"submitted_us\":6004,\"executed_us\":7004") != std::string::npos);
}

DOCTEST_TEST_CASE("Socket and composed rings share one bounded admission and freeze together") {
    constexpr auto socket_state_bytes = 1024;
    operation_recorder recorder(true, (std::numeric_limits<std::size_t>::max)(), true, socket_state_bytes);
    DOCTEST_CHECK(recorder.slots() == operation_recorder::maximum_slots(true, socket_state_bytes));
    DOCTEST_CHECK(recorder.slots() >= 5000);
    DOCTEST_CHECK(recorder.storage_bytes() + recorder.socket_state_reserved_bytes() + trace_output::limit +
                      operation_recorder::scratch_reservation <=
                  operation_recorder::memory_limit);
    operation_ring* socket{};
    auto* composed = recorder.acquire(&socket);
    DOCTEST_REQUIRE(composed);
    DOCTEST_REQUIRE(socket);
    DOCTEST_CHECK(composed != socket);
    composed->record(operation_phase::read_start, 1, 0, 4);
    socket->record(operation_phase::read_start, 2, 4096, 4);
    recorder.freeze();
    const auto before = json(*socket);
    socket->record(operation_phase::read_complete, 3, 1091, 4);
    DOCTEST_CHECK(json(*socket) == before);
    DOCTEST_CHECK(composed->frozen());
    DOCTEST_CHECK(json(recorder).find("\"low_level_operations\":true") != std::string::npos);
    operation_recorder disabled(false, 5000, true, socket_state_bytes);
    socket = composed;
    DOCTEST_CHECK(disabled.acquire(&socket) == nullptr);
    DOCTEST_CHECK(socket == nullptr);
    DOCTEST_CHECK(disabled.socket_state_reserved_bytes() == 0);
    DOCTEST_CHECK(disabled.storage_bytes() == sizeof(operation_recorder));
    operation_recorder composed_only(true, 1);
    socket = composed;
    DOCTEST_CHECK(composed_only.acquire(&socket) != nullptr);
    DOCTEST_CHECK(socket == nullptr);
    DOCTEST_CHECK(composed_only.socket_state_reserved_bytes() == 0);
    DOCTEST_CHECK(composed_only.storage_bytes() == sizeof(operation_recorder) + sizeof(operation_ring));
}

DOCTEST_TEST_CASE("Trace output stays bounded, rejects partial records and reserves a complete summary") {
    trace_output output;
    const auto write = [](std::ostream& out) { out << "{\"fixed\":\"" << std::string(6000, 'x') << "\"}"; };
    unsigned retained{};
    while (output.append(write))
    {
        ++retained;
    }
    DOCTEST_CHECK(retained > 16);
    DOCTEST_CHECK(output.truncated());
    const auto before = output.text();
    DOCTEST_CHECK_FALSE(output.append([](std::ostream& out) { out << std::string(9000, 'x'); }));
    DOCTEST_CHECK(output.text() == before);
    DOCTEST_CHECK(
        output.append([](std::ostream& out) { out << "{\"summary\":true,\"output_truncated\":true}"; }, true));
    DOCTEST_CHECK(output.text().size() <= trace_output::limit);
    DOCTEST_CHECK(output.text().ends_with("{\"summary\":true,\"output_truncated\":true}\n"));
    DOCTEST_CHECK(output.text().find(std::string(8192, 'x')) == std::string::npos);
}

DOCTEST_TEST_CASE("Both full operation rings fit one complete failure record") {
    operation_ring composed, socket;
    for (std::uint64_t index = 0; index < 8; ++index)
    {
        const auto sequence = (std::numeric_limits<std::uint64_t>::max)() - index;
        const auto time = (std::numeric_limits<std::int64_t>::max)() - 16 + 2 * index;
        composed.record(operation_phase::read_start, time, 0, sequence);
        socket.record(operation_phase::read_start, time, 4096, sequence);
        socket.record(operation_phase::read_complete, time + 1, 1091, sequence, 125);
        composed.record(operation_phase::read_complete, time + 1, 1091, sequence, 125);
    }
    trace_output output;
    DOCTEST_REQUIRE(output.append([&](std::ostream& out) {
        out << "{\"trace\":{";
        composed.write_json(out);
        out << "},\"socket_trace\":{";
        socket.write_json(out);
        out << "}}";
    }));
    DOCTEST_CHECK_FALSE(output.truncated());
    DOCTEST_CHECK(output.text().ends_with("}}\n"));
    DOCTEST_CHECK(output.text().size() < trace_record_buffer::capacity);
}
