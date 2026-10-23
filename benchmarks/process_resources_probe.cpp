// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <latch>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/mman.h>

#endif

#include "process_resources.hpp"

namespace {

using arknet::benchmark::process_resource_sampler;
using arknet::benchmark::process_resources;
using arknet::benchmark::resource_metric;
using clock_type = std::chrono::steady_clock;

void json_string(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::cout << '"';
    for (const unsigned char character : value)
    {
        if (character == '"' || character == '\\')
        {
            std::cout << '\\' << static_cast<char>(character);
        }
        else if (character < 0x20)
        {
            std::cout << "\\u00" << hex[character >> 4] << hex[character & 15];
        }
        else
        {
            std::cout << static_cast<char>(character);
        }
    }
    std::cout << '"';
}

template<class value_type>
void write_metric(std::string_view name, const resource_metric<value_type>& metric) {
    std::cout << ',';
    json_string(name);
    std::cout << ":{\"value\":";
    if (metric.value)
    {
        std::cout << *metric.value;
    }
    else
    {
        std::cout << "null";
    }
    std::cout << ",\"unavailable_reason\":";
    json_string(metric.unavailable_reason);
    std::cout << '}';
}

void write_sample(const process_resources& sample, clock_type::time_point first, std::uint64_t index) {
    std::cout << "{\"event\":\"process_resources\",\"sample\":" << index
              << ",\"elapsed_seconds\":" << std::chrono::duration<double>(sample.monotonic_time - first).count();
    write_metric("cpu_percent", sample.cpu_percent);
    write_metric("current_rss_bytes", sample.current_rss_bytes);
    write_metric("peak_rss_bytes", sample.peak_rss_bytes);
    write_metric("fd_count", sample.fd_count);
    write_metric("nofile_soft", sample.nofile_soft);
    write_metric("nofile_hard", sample.nofile_hard);
    write_metric("handle_count", sample.handle_count);
    write_metric("thread_count", sample.thread_count);
    std::cout << "}\n";
}

void require(bool condition, std::string_view description, unsigned& checks) {
    if (!condition)
    {
        throw std::runtime_error(std::string(description));
    }
    ++checks;
}

template<class value_type>
void check_metric(const resource_metric<value_type>& metric, unsigned& checks) {
    require(
        metric.value.has_value() == metric.unavailable_reason.empty(), "Metric value/reason contract failed", checks);
}

void check_shape(const process_resources& sample, unsigned& checks) {
    check_metric(sample.cpu_percent, checks);
    check_metric(sample.current_rss_bytes, checks);
    check_metric(sample.peak_rss_bytes, checks);
    check_metric(sample.fd_count, checks);
    check_metric(sample.nofile_soft, checks);
    check_metric(sample.nofile_hard, checks);
    check_metric(sample.handle_count, checks);
    check_metric(sample.thread_count, checks);
    if (sample.cpu_percent.value)
    {
        require(std::isfinite(*sample.cpu_percent.value) && *sample.cpu_percent.value >= 0,
            "CPU percentage must be finite and nonnegative", checks);
    }
}

class resident_allocation
{
public:
    static constexpr std::size_t size = 32 * 1024 * 1024;

    resident_allocation() {
#ifdef _WIN32
        address_ = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__APPLE__) || defined(__linux__)
        address_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (address_ == MAP_FAILED)
        {
            address_ = nullptr;
        }
#endif
        if (address_)
        {
            auto* bytes = static_cast<volatile unsigned char*>(address_);
            for (std::size_t offset = 0; offset < size; offset += 4096)
            {
                bytes[offset] = 1;
            }
        }
    }

    resident_allocation(const resident_allocation&) = delete;
    resident_allocation& operator=(const resident_allocation&) = delete;

    ~resident_allocation() {
        if (address_)
        {
#ifdef _WIN32
            VirtualFree(address_, 0, MEM_RELEASE);
#elif defined(__APPLE__) || defined(__linux__)
            munmap(address_, size);
#endif
        }
    }

    bool available() const {
        return address_ != nullptr;
    }

private:
    void* address_ = nullptr;
};

void self_check() {
    unsigned checks = 0;
    using arknet::benchmark::process_resource_detail::interval_cpu_percent;
    require(interval_cpu_percent(1, 2, 1).value == 100.0, "One core must equal 100 percent", checks);
    require(interval_cpu_percent(1, 3, 1).value == 200.0, "CPU percentage must not be clamped at 100", checks);
    require(interval_cpu_percent(2, 2, 1).value == 0.0, "Idle CPU interval must remain a real zero", checks);
    for (const auto& value :
        {interval_cpu_percent(1, 2, 0), interval_cpu_percent(1, 2, -1), interval_cpu_percent(2, 1, 1),
            interval_cpu_percent(-1, 2, 1), interval_cpu_percent(1, (std::numeric_limits<double>::infinity)(), 1)})
    {
        require(!value.value && !value.unavailable_reason.empty(), "Invalid CPU interval needs a reason", checks);
    }

    process_resource_sampler sampler;
    const auto first = sampler.sample();
    check_shape(first, checks);
    require(!first.cpu_percent.value, "First CPU sample must not invent a percentage", checks);
    for (int round = 0; round < 16; ++round)
    {
        const auto next = sampler.sample();
        check_shape(next, checks);
        require(next.monotonic_time >= first.monotonic_time, "Monotonic time moved backwards", checks);
        if (first.fd_count.value && next.fd_count.value)
        {
            require(*next.fd_count.value == *first.fd_count.value, "Repeated sampling leaked or counted a temporary FD",
                checks);
        }
    }

    const auto before_files = sampler.sample();
    process_resources open_files;
    {
        std::vector<std::unique_ptr<std::FILE, decltype(&std::fclose)>> files;
        for (int index = 0; index < 8; ++index)
        {
            files.emplace_back(std::tmpfile(), &std::fclose);
            require(files.back() != nullptr, "Could not create an owned temporary file", checks);
        }
        open_files = sampler.sample();
        check_shape(open_files, checks);
        if (before_files.fd_count.value && open_files.fd_count.value)
        {
            require(*open_files.fd_count.value >= *before_files.fd_count.value + files.size(),
                "Owned files were not reflected in the FD count", checks);
        }
        if (before_files.handle_count.value && open_files.handle_count.value)
        {
            require(*open_files.handle_count.value >= *before_files.handle_count.value + files.size(),
                "Owned files were not reflected in the Windows handle count", checks);
        }
    }
    const auto closed_files = sampler.sample();
    check_shape(closed_files, checks);
    if (open_files.fd_count.value && closed_files.fd_count.value)
    {
        require(*open_files.fd_count.value >= *closed_files.fd_count.value + 8, "Owned FDs did not close", checks);
    }
    if (open_files.handle_count.value && closed_files.handle_count.value)
    {
        require(*open_files.handle_count.value >= *closed_files.handle_count.value + 8, "Owned handles did not close",
            checks);
    }

    const auto before_threads = sampler.sample();
    {
        std::latch started(2);
        const auto work = [&started](std::stop_token stop) {
            started.count_down();
            while (!stop.stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        };
        std::jthread first_worker(work);
        std::jthread second_worker(work);
        started.wait();
        const auto running = sampler.sample();
        check_shape(running, checks);
        if (before_threads.thread_count.value && running.thread_count.value)
        {
            require(*running.thread_count.value >= *before_threads.thread_count.value + 2,
                "Owned workers were not reflected in the thread count", checks);
        }
    }

    bool rss_release_checked = false;
    process_resources resident;
    {
        resident_allocation allocation;
        resident = sampler.sample();
        check_shape(resident, checks);
        rss_release_checked = allocation.available() && resident.current_rss_bytes.value.has_value();
    }
    const auto released = sampler.sample();
    check_shape(released, checks);
    if (rss_release_checked && released.current_rss_bytes.value)
    {
        require(*resident.current_rss_bytes.value >= *released.current_rss_bytes.value + resident_allocation::size / 2,
            "Current RSS did not distinguish released memory from peak RSS", checks);
    }
    else
    {
        rss_release_checked = false;
    }

    sampler.sample();
    std::uint64_t work = 1;
    const auto until = clock_type::now() + std::chrono::milliseconds(150);
    while (clock_type::now() < until)
    {
        for (int index = 0; index < 1024; ++index)
        {
            work = work * 6364136223846793005ULL + 1;
        }
    }
    static std::atomic<std::uint64_t> sink;
    sink.store(work, std::memory_order_relaxed);
    const auto busy = sampler.sample();
    check_shape(busy, checks);
    if (busy.cpu_percent.value)
    {
        require(*busy.cpu_percent.value > 0, "Owned CPU work was not reflected in interval CPU", checks);
    }
    std::cout << "{\"event\":\"process_resources_self_check\",\"checks\":" << checks
              << ",\"rss_release_checked\":" << (rss_release_checked ? "true" : "false") << "}\n";
    write_sample(first, first.monotonic_time, 0);
    write_sample(busy, first.monotonic_time, 1);
}

unsigned parse_number(std::string_view text, unsigned minimum, unsigned maximum) {
    unsigned value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < minimum || value > maximum)
    {
        throw std::invalid_argument("Numeric argument is outside its allowed range");
    }
    return value;
}

} // namespace

int main(int argc, char** argv) {
    std::cout << std::setprecision(9);
    try
    {
        unsigned samples = 5, interval_ms = 1000;
        bool check = false;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--help")
            {
                std::cout << "Usage: arknet_process_resources_probe [--samples 1..3600] [--interval-ms 100..60000]\n"
                             "       arknet_process_resources_probe --self-check\n"
                             "Samples only this process; JSON Lines output. CPU 100% means one logical core.\n";
                return 0;
            }
            if (argument == "--self-check")
            {
                check = true;
            }
            else if ((argument == "--samples" || argument == "--interval-ms") && index + 1 < argc)
            {
                const auto value = std::string_view(argv[++index]);
                if (argument == "--samples")
                {
                    samples = parse_number(value, 1, 3600);
                }
                else
                {
                    interval_ms = parse_number(value, 100, 60000);
                }
            }
            else
            {
                throw std::invalid_argument("Unknown argument or missing value; use --help");
            }
        }
        if (check)
        {
            self_check();
            return 0;
        }
        process_resource_sampler sampler;
        const auto first = sampler.sample();
        write_sample(first, first.monotonic_time, 0);
        for (unsigned index = 1; index < samples; ++index)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            write_sample(sampler.sample(), first.monotonic_time, index);
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "process resources probe: " << error.what() << '\n';
        return 1;
    }
}
