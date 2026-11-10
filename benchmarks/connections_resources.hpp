// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <ostream>
#include <string_view>

#include "connections_ports.hpp"
#include "process_resources.hpp"

namespace connection_load {

struct resource_snapshot {
    arknet::benchmark::process_resources metrics;
    double elapsed_seconds{};
    std::optional<double> cpu_interval_seconds;
};

// Keep independent observers for periodic intervals and the entire run.
class resource_observer {
public:
    explicit resource_observer(std::chrono::steady_clock::time_point began)
        : began_(began)
        , initial_(sampler_.sample())
        , previous_(initial_.monotonic_time) {}

    resource_snapshot initial_snapshot() const {
        return {initial_, std::chrono::duration<double>(initial_.monotonic_time - began_).count(), std::nullopt};
    }

    resource_snapshot sample() {
        auto metrics = sampler_.sample();
        const auto elapsed = std::chrono::duration<double>(metrics.monotonic_time - began_).count();
        std::optional<double> interval;
        if (metrics.cpu_percent.value) {
            interval = std::chrono::duration<double>(metrics.monotonic_time - previous_).count();
        }
        previous_ = metrics.monotonic_time;
        return {std::move(metrics), elapsed, interval};
    }

private:
    std::chrono::steady_clock::time_point began_;
    arknet::benchmark::process_resource_sampler sampler_;
    arknet::benchmark::process_resources initial_;
    std::chrono::steady_clock::time_point previous_;
};

inline void write_resource_string(std::ostream& output, std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    output << '"';
    for (const unsigned char character : value) {
        if (character == '"' || character == '\\') {
            output << '\\';
        }
        if (character < 32) {
            output << "\\u00" << hex[character >> 4] << hex[character & 15];
        } else {
            output << static_cast<char>(character);
        }
    }
    output << '"';
}

inline void write_port_resources(std::ostream& output, const port_resources& sample) {
    output << "{\"ephemeral_ports\":";
    if (sample.ephemeral_ports.value) {
        output << "{\"first\":" << sample.ephemeral_ports.value->first
               << ",\"last\":" << sample.ephemeral_ports.value->last << '}';
    } else {
        output << "null";
    }
    output << ",\"range_source\":";
    write_resource_string(output, sample.range_source);
    output << ",\"range_scope\":";
    write_resource_string(output, sample.range_scope);
    output << ",\"tcp_time_wait\":";
    if (sample.tcp_time_wait.value) {
        output << *sample.tcp_time_wait.value;
    } else {
        output << "null";
    }
    output << ",\"time_wait_source\":";
    write_resource_string(output, sample.time_wait_source);
    output << ",\"time_wait_scope\":";
    write_resource_string(output, sample.time_wait_scope);
    output << ",\"unavailable\":{\"ephemeral_ports\":";
    write_resource_string(output, sample.ephemeral_ports.unavailable_reason);
    output << ",\"tcp_time_wait\":";
    write_resource_string(output, sample.tcp_time_wait.unavailable_reason);
    output << "}}";
}

// First failure is sampled at the connect callback, before cleanup changes the
// socket population. Final output does not turn this into an after-cleanup count.
class connect_failure_observer {
public:
    template<class Query>
    void capture(int error, std::string_view category, std::uint64_t established, Query query) noexcept {
        try {
            const std::lock_guard lock(mutex_);
            if (attempted_) {
                return;
            }
            attempted_ = true;
            error_ = error;
            established_ = established;
            category_ = category;
            sample_ = query();
        } catch (...) {
        }
    }

    void capture(int error, std::string_view category, std::uint64_t established) noexcept {
        capture(error, category, established, read_port_resources);
    }

    void write_fields(std::ostream& output) const {
        const std::lock_guard lock(mutex_);
        write_fields_unlocked(output);
    }

    void write_event(std::ostream& output, std::string_view role, std::uint64_t run, std::uint64_t client,
        bool string_ids = false) const {
        const std::lock_guard lock(mutex_);
        if (!attempted_) {
            return;
        }
        output << "{\"event\":\"connect_resource_failure\",\"role\":";
        write_resource_string(output, role);
        const auto quote = string_ids ? "\"" : "";
        output << ",\"run_id\":" << quote << run << quote << ",\"client_id\":" << quote << client << quote;
        write_fields_unlocked(output);
        output << "}\n" << std::flush;
    }

private:
    void write_fields_unlocked(std::ostream& output) const {
        output << ",\"connect_failure_resources\":";
        if (!attempted_) {
            output << "null";
            return;
        }
        output << "{\"phase\":\"first_connect_failure_before_cleanup\",\"established\":" << established_
               << ",\"error_code\":" << error_ << ",\"error_category\":";
        write_resource_string(output, category_);
        output << ",\"network_resources\":";
        if (sample_) {
            write_port_resources(output, *sample_);
        } else {
            output << "{\"unavailable\":\"Failure resource snapshot could not be captured\"}";
        }
        output << '}';
    }

    mutable std::mutex mutex_;
    bool attempted_{};
    int error_{};
    std::uint64_t established_{};
    std::string category_;
    std::optional<port_resources> sample_;
};

inline void write_resource_fields(std::ostream& output, const resource_snapshot& sample, bool after_cleanup) {
    const auto metric = [&](std::string_view name, const auto& value) {
        output << ',';
        write_resource_string(output, name);
        output << ':';
        if (value) {
            output << *value;
        } else {
            output << "null";
        }
    };
    metric("cpu_percent", sample.metrics.cpu_percent.value);
    metric("peak_rss_bytes", sample.metrics.peak_rss_bytes.value);
    metric("current_rss_bytes", sample.metrics.current_rss_bytes.value);
    metric("fd_count", sample.metrics.fd_count.value);
    metric("nofile_soft", sample.metrics.nofile_soft.value);
    metric("nofile_hard", sample.metrics.nofile_hard.value);
    metric("handle_count", sample.metrics.handle_count.value);
    metric("thread_count", sample.metrics.thread_count.value);
    metric("resource_cpu_interval_s", sample.cpu_interval_seconds);
    output << ",\"resource_sample_elapsed_s\":" << sample.elapsed_seconds
           << ",\"resource_phase\":" << (after_cleanup ? "\"after_cleanup\"" : "\"running\"")
           << ",\"resource_unavailable\":{";
    bool first = true;
    const auto reason = [&](std::string_view name, const auto& value) {
        if (value.value) {
            return;
        }
        if (!first) {
            output << ',';
        }
        first = false;
        write_resource_string(output, name);
        output << ':';
        write_resource_string(output, value.unavailable_reason);
    };
    reason("cpu_percent", sample.metrics.cpu_percent);
    reason("peak_rss_bytes", sample.metrics.peak_rss_bytes);
    reason("current_rss_bytes", sample.metrics.current_rss_bytes);
    reason("fd_count", sample.metrics.fd_count);
    reason("nofile_soft", sample.metrics.nofile_soft);
    reason("nofile_hard", sample.metrics.nofile_hard);
    reason("handle_count", sample.metrics.handle_count);
    reason("thread_count", sample.metrics.thread_count);
    if (!sample.cpu_interval_seconds) {
        if (!first) {
            output << ',';
        }
        output << "\"resource_cpu_interval_s\":\"A valid interval CPU percentage is unavailable\"";
    }
    output << '}';
}

inline void write_resource_startup(std::ostream& output, const resource_snapshot& sample, std::string_view role,
    std::uint64_t run, std::uint64_t client, bool string_ids = false) {
    output << "{\"event\":\"resource_startup\",\"role\":";
    write_resource_string(output, role);
    const auto quote = string_ids ? "\"" : "";
    output << ",\"run_id\":" << quote << run << quote << ",\"client_id\":" << quote << client << quote;
    write_resource_fields(output, sample, false);
    output << ",\"network_resources\":";
    write_port_resources(output, read_port_resources());
    output << "}\n" << std::flush;
}

} // namespace connection_load
