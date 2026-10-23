// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>
#include <utility>

namespace connection_load {

enum class operation_phase
{
    read_start,
    read_complete,
    write_start,
    write_complete
};

inline const char* phase_name(operation_phase phase) noexcept {
    switch (phase)
    {
    case operation_phase::read_start:
        return "read_start";
    case operation_phase::read_complete:
        return "read_complete";
    case operation_phase::write_start:
        return "write_start";
    case operation_phase::write_complete:
        return "write_complete";
    }
    return "unknown";
}

struct operation_event
{
    std::uint64_t ordinal{}, operation_id{}, bytes{}, sequence{};
    std::int64_t time_us{};
    int error{};
    operation_phase phase{};
};

class operation_ring
{
public:
    static constexpr std::size_t capacity = 16;

    void record(operation_phase phase, std::int64_t time_us, std::uint64_t bytes, std::uint64_t sequence,
        int error = 0) noexcept {
        if (frozen_)
        {
            return;
        }
        const bool read = phase == operation_phase::read_start || phase == operation_phase::read_complete;
        const bool start = phase == operation_phase::read_start || phase == operation_phase::write_start;
        auto& pending = read ? read_id_ : write_id_;
        if (start)
        {
            if (pending)
            {
                ++invalid_transitions_;
            }
            pending = ++operations_;
        }
        else if (!pending)
        {
            ++invalid_transitions_;
        }
        events_[total_ % capacity] = {total_ + 1, pending, bytes, sequence, time_us, error, phase};
        ++total_;
        if (!start)
        {
            (read ? read_bytes_ : write_bytes_) += bytes;
            pending = 0;
        }
    }

    void freeze() noexcept {
        frozen_ = true;
    }

    bool frozen() const noexcept {
        return frozen_;
    }

    std::size_t size() const noexcept {
        return (std::min)(total_, std::uint64_t{capacity});
    }

    std::uint64_t overwritten() const noexcept {
        return total_ - size();
    }

    std::uint64_t invalid_transitions() const noexcept {
        return invalid_transitions_;
    }

    bool history_complete() const noexcept {
        return !overwritten() && !invalid_transitions_;
    }

    const operation_event& at(std::size_t index) const noexcept {
        return events_[(total_ - size() + index) % capacity];
    }

    void write_json(std::ostream& out) const {
        out << "\"frozen\":" << (frozen_ ? "true" : "false")
            << ",\"history_complete\":" << (history_complete() ? "true" : "false")
            << ",\"overwritten_events\":" << overwritten() << ",\"invalid_transitions\":" << invalid_transitions_
            << ",\"pending_read_id\":" << read_id_ << ",\"pending_write_id\":" << write_id_
            << ",\"completed_read_bytes\":" << read_bytes_ << ",\"completed_write_bytes\":" << write_bytes_
            << ",\"events\":[";
        for (std::size_t index = 0; index < size(); ++index)
        {
            const auto& event = at(index);
            if (index)
            {
                out << ',';
            }
            out << "{\"ordinal\":" << event.ordinal << ",\"operation_id\":" << event.operation_id << ",\"phase\":\""
                << phase_name(event.phase) << "\",\"time_us\":" << event.time_us << ",\"bytes\":" << event.bytes
                << ",\"sequence\":" << event.sequence << ",\"error\":" << event.error << '}';
        }
        out << ']';
    }

private:
    std::array<operation_event, capacity> events_{};
    std::uint64_t total_{}, operations_{}, read_id_{}, write_id_{}, read_bytes_{}, write_bytes_{},
        invalid_transitions_{};
    bool frozen_{};
};

// Fixed scratch space prevents an oversized record from allocating or emitting
// a partial JSON line. Callers serialize only numeric fields and fixed labels.
class trace_record_buffer : public std::streambuf
{
public:
    static constexpr std::size_t capacity = 8192;

    trace_record_buffer() {
        setp(bytes_.data(), bytes_.data() + bytes_.size());
    }

    std::string_view view() const noexcept {
        return {pbase(), static_cast<std::size_t>(pptr() - pbase())};
    }

private:
    std::array<char, capacity> bytes_{};
};

class trace_output
{
public:
    static constexpr std::size_t limit = 256 * 1024;

    trace_output() {
        text_.reserve(limit);
    }

    template<class Write>
    bool append(Write write, bool summary = false) {
        trace_record_buffer record;
        std::ostream out(&record);
        write(out);
        const auto bytes = record.view();
        const auto maximum = summary ? limit : limit - trace_record_buffer::capacity;
        if (!out || bytes.size() + 1 > maximum || text_.size() > maximum - bytes.size() - 1)
        {
            truncated_ = true;
            return false;
        }
        text_.append(bytes);
        text_ += '\n';
        return true;
    }

    bool truncated() const noexcept {
        return truncated_;
    }

    const std::string& text() const noexcept {
        return text_;
    }

private:
    std::string text_;
    bool truncated_{};
};

class operation_recorder
{
public:
    static constexpr std::size_t memory_limit = 16 * 1024 * 1024;
    static constexpr std::size_t scratch_reservation = 32 * 1024;
    static constexpr std::size_t marker_capacity = 16;

    static constexpr std::size_t maximum_slots() noexcept {
        return (memory_limit - sizeof(operation_recorder) - trace_output::limit - scratch_reservation) /
               sizeof(operation_ring);
    }

    operation_recorder(bool enabled, std::size_t sessions)
        : enabled_(enabled)
        , capacity_(enabled ? (std::min)(sessions, maximum_slots()) : 0)
        , rings_(capacity_ ? std::make_unique<operation_ring[]>(capacity_) : nullptr) {}

    operation_ring* acquire() noexcept {
        if (!enabled_ || frozen_)
        {
            return nullptr;
        }
        if (used_ == capacity_)
        {
            ++unrecorded_;
            return nullptr;
        }
        return &rings_[used_++];
    }

    bool enabled() const noexcept {
        return enabled_;
    }

    bool frozen() const noexcept {
        return frozen_;
    }

    std::size_t slots() const noexcept {
        return capacity_;
    }

    std::size_t storage_bytes() const noexcept {
        return sizeof(*this) + capacity_ * sizeof(operation_ring);
    }

    std::size_t unrecorded_sessions() const noexcept {
        return unrecorded_;
    }

    // Controller thread: allow at most one queued marker even if the IO worker
    // stops making progress. No marker is posted in disabled mode.
    bool request_marker() noexcept {
        if (!enabled_)
        {
            return false;
        }
        if (marker_pending_.exchange(true))
        {
            ++marker_skipped_;
            return false;
        }
        ++marker_submitted_;
        return true;
    }

    // Single IO worker only, like all session ring writes and freeze().
    void complete_marker(std::int64_t submitted_us, std::int64_t executed_us) noexcept {
        if (!enabled_ || frozen_)
        {
            return;
        }
        markers_[marker_completed_ % marker_capacity] = {submitted_us, executed_us};
        ++marker_completed_;
        marker_pending_ = false;
    }

    void freeze() noexcept {
        frozen_ = true;
        for (std::size_t index = 0; index < used_; ++index)
        {
            rings_[index].freeze();
        }
    }

    void write_json(std::ostream& out) const {
        out << "\"operation_scope\":\"composed_only\",\"low_level_operations\":false,\"frozen\":"
            << (frozen_ ? "true" : "false") << ",\"ring_capacity\":" << operation_ring::capacity
            << ",\"session_slots\":" << capacity_ << ",\"recorded_sessions\":" << used_
            << ",\"unrecorded_sessions\":" << unrecorded_ << ",\"storage_bytes\":" << storage_bytes()
            << ",\"memory_limit_bytes\":" << memory_limit << ",\"output_limit_bytes\":" << trace_output::limit
            << ",\"worker_markers_submitted\":" << marker_submitted_.load()
            << ",\"worker_markers_skipped\":" << marker_skipped_.load()
            << ",\"worker_marker_pending\":" << (marker_pending_ ? "true" : "false")
            << ",\"worker_markers_completed\":" << marker_completed_ << ",\"worker_history_complete\":"
            << (marker_completed_ <= marker_capacity && !marker_pending_ && !marker_skipped_ ? "true" : "false")
            << ",\"worker_overwritten_markers\":"
            << (marker_completed_ > marker_capacity ? marker_completed_ - marker_capacity : 0)
            << ",\"worker_markers\":[";
        const auto count = (std::min)(marker_completed_, std::uint64_t{marker_capacity});
        for (std::uint64_t index = 0; index < count; ++index)
        {
            const auto& marker = markers_[(marker_completed_ - count + index) % marker_capacity];
            if (index)
            {
                out << ',';
            }
            out << "{\"submitted_us\":" << marker.first << ",\"executed_us\":" << marker.second << '}';
        }
        out << ']';
    }

private:
    bool enabled_, frozen_{};
    std::size_t capacity_, used_{}, unrecorded_{};
    std::unique_ptr<operation_ring[]> rings_;
    std::array<std::pair<std::int64_t, std::int64_t>, marker_capacity> markers_{};
    std::atomic<bool> marker_pending_{};
    std::atomic<std::uint64_t> marker_submitted_{}, marker_skipped_{};
    std::uint64_t marker_completed_{};
};
} // namespace connection_load
