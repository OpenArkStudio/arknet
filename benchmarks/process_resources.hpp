// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0603
#include <processsnapshot.h>
#endif
#elif defined(__APPLE__)
#include <sys/resource.h>
#include <unistd.h>

#include <libproc.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>

#include <dirent.h>
#endif

namespace arknet::benchmark {

template<class value_type>
struct resource_metric
{
    std::optional<value_type> value;
    std::string unavailable_reason;
};

struct process_resources
{
    std::chrono::steady_clock::time_point monotonic_time;
    resource_metric<double> cpu_percent;
    resource_metric<std::uint64_t> current_rss_bytes;
    resource_metric<std::uint64_t> peak_rss_bytes;
    resource_metric<std::uint64_t> fd_count;
    resource_metric<std::uint64_t> handle_count;
    resource_metric<std::uint64_t> thread_count;
};

namespace process_resource_detail {

template<class value_type>
resource_metric<value_type> unavailable(std::string reason) {
    return {std::nullopt, std::move(reason)};
}

inline std::string system_failure(const char* operation, int error) {
    // Keep diagnostics ASCII so JSON output does not depend on the Windows code page.
    return std::string(operation) + " failed (OS error " + std::to_string(error) + ")";
}

inline resource_metric<double> interval_cpu_percent(double previous, double current, double seconds) {
    if (!std::isfinite(previous) || !std::isfinite(current) || previous < 0 || current < 0)
    {
        return unavailable<double>("Process CPU counter is not finite and nonnegative");
    }
    if (!std::isfinite(seconds) || seconds <= 0)
    {
        return unavailable<double>("Monotonic sampling interval is not positive and finite");
    }
    if (current < previous)
    {
        return unavailable<double>("Process CPU counter moved backwards");
    }
    const double percentage = 100.0 * ((current - previous) / seconds);
    if (!std::isfinite(percentage))
    {
        return unavailable<double>("CPU percentage is outside the representable range");
    }
    return {percentage, {}};
}

inline resource_metric<double> read_cpu(process_resources& result) {
#ifdef _WIN32
    (void)result;
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
    {
        return unavailable<double>(system_failure("GetProcessTimes", static_cast<int>(GetLastError())));
    }
    const auto seconds = [](FILETIME value) {
        const auto ticks = (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
        return static_cast<double>(ticks) / 10000000.0;
    };
    return {seconds(kernel) + seconds(user), {}};
#elif defined(__APPLE__) || defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0)
    {
        const auto reason = system_failure("getrusage(RUSAGE_SELF)", errno);
        result.peak_rss_bytes = unavailable<std::uint64_t>(reason);
        return unavailable<double>(reason);
    }
    constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
#ifdef __APPLE__
    constexpr std::uint64_t rss_scale = 1;
#else
    constexpr std::uint64_t rss_scale = 1024;
#endif
    if (usage.ru_maxrss < 0 || static_cast<std::uint64_t>(usage.ru_maxrss) > maximum / rss_scale)
    {
        result.peak_rss_bytes = unavailable<std::uint64_t>("getrusage returned an invalid peak RSS");
    }
    else
    {
        result.peak_rss_bytes = {static_cast<std::uint64_t>(usage.ru_maxrss) * rss_scale, {}};
    }
    const double seconds =
        static_cast<double>(usage.ru_utime.tv_sec) + static_cast<double>(usage.ru_stime.tv_sec) +
        (static_cast<double>(usage.ru_utime.tv_usec) + static_cast<double>(usage.ru_stime.tv_usec)) / 1000000.0;
    return {seconds, {}};
#else
    result.peak_rss_bytes = unavailable<std::uint64_t>("Peak RSS is unsupported on this platform");
    return unavailable<double>("Process CPU sampling is unsupported on this platform");
#endif
}

#ifdef __APPLE__
inline resource_metric<std::uint64_t> read_fd_count() {
    errno = 0;
    const int required = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (required < 0 || (required == 0 && errno != 0))
    {
        return unavailable<std::uint64_t>(system_failure("proc_pidinfo(PROC_PIDLISTFDS)", errno));
    }
    std::size_t capacity = static_cast<std::size_t>(required) / sizeof(proc_fdinfo) + 32;
    for (int attempt = 0; attempt != 3; ++attempt)
    {
        if (capacity > static_cast<std::size_t>((std::numeric_limits<int>::max)()) / sizeof(proc_fdinfo))
        {
            return unavailable<std::uint64_t>("FD list exceeds the proc_pidinfo buffer size limit");
        }
        std::vector<proc_fdinfo> entries(capacity);
        const auto bytes = static_cast<int>(entries.size() * sizeof(proc_fdinfo));
        errno = 0;
        const int received = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, entries.data(), bytes);
        if (received < 0 || (received == 0 && errno != 0))
        {
            return unavailable<std::uint64_t>(system_failure("proc_pidinfo(PROC_PIDLISTFDS)", errno));
        }
        if (received % sizeof(proc_fdinfo) != 0)
        {
            return unavailable<std::uint64_t>("proc_pidinfo returned an incomplete FD record");
        }
        if (received < bytes)
        {
            return {static_cast<std::uint64_t>(received / sizeof(proc_fdinfo)), {}};
        }
        capacity *= 2;
    }
    return unavailable<std::uint64_t>("FD list did not fit during three bounded sampling attempts");
}
#elif defined(__linux__)
inline resource_metric<std::uint64_t> read_fd_count() {
    std::unique_ptr<DIR, decltype(&closedir)> directory(opendir("/proc/self/fd"), &closedir);
    if (!directory)
    {
        return unavailable<std::uint64_t>(system_failure("opendir(/proc/self/fd)", errno));
    }
    const int own_fd = dirfd(directory.get());
    if (own_fd < 0)
    {
        return unavailable<std::uint64_t>(system_failure("dirfd(/proc/self/fd)", errno));
    }
    std::uint64_t count = 0;
    errno = 0;
    while (const auto* entry = readdir(directory.get()))
    {
        char* end = nullptr;
        const auto descriptor = std::strtoll(entry->d_name, &end, 10);
        if (end != entry->d_name && *end == '\0' && descriptor >= 0 && descriptor != own_fd)
        {
            ++count;
        }
        errno = 0;
    }
    if (errno != 0)
    {
        return unavailable<std::uint64_t>(system_failure("readdir(/proc/self/fd)", errno));
    }
    return {count, {}};
}
#endif

inline void read_memory_and_counts(process_resources& result) {
#ifdef _WIN32
    result.fd_count = unavailable<std::uint64_t>("POSIX file-descriptor count is unavailable on Windows");
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
    {
        result.current_rss_bytes = {static_cast<std::uint64_t>(memory.WorkingSetSize), {}};
        result.peak_rss_bytes = {static_cast<std::uint64_t>(memory.PeakWorkingSetSize), {}};
    }
    else
    {
        const auto reason = system_failure("GetProcessMemoryInfo", static_cast<int>(GetLastError()));
        result.current_rss_bytes = unavailable<std::uint64_t>(reason);
        result.peak_rss_bytes = unavailable<std::uint64_t>(reason);
    }
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles))
    {
        result.handle_count = {handles, {}};
    }
    else
    {
        result.handle_count =
            unavailable<std::uint64_t>(system_failure("GetProcessHandleCount", static_cast<int>(GetLastError())));
    }
#if defined(_WIN32_WINNT) && _WIN32_WINNT >= 0x0603
    HPSS snapshot = nullptr;
    const DWORD captured = PssCaptureSnapshot(GetCurrentProcess(), PSS_CAPTURE_THREADS, 0, &snapshot);
    if (captured == ERROR_SUCCESS)
    {
        PSS_THREAD_INFORMATION threads{};
        const DWORD queried = PssQuerySnapshot(snapshot, PSS_QUERY_THREAD_INFORMATION, &threads, sizeof(threads));
        const DWORD freed = PssFreeSnapshot(GetCurrentProcess(), snapshot);
        if (queried != ERROR_SUCCESS)
        {
            result.thread_count =
                unavailable<std::uint64_t>(system_failure("PssQuerySnapshot", static_cast<int>(queried)));
        }
        else if (freed != ERROR_SUCCESS)
        {
            result.thread_count =
                unavailable<std::uint64_t>(system_failure("PssFreeSnapshot", static_cast<int>(freed)));
        }
        else
        {
            result.thread_count = {threads.ThreadsCaptured, {}};
        }
    }
    else
    {
        result.thread_count =
            unavailable<std::uint64_t>(system_failure("PssCaptureSnapshot", static_cast<int>(captured)));
    }
#else
    result.thread_count = unavailable<std::uint64_t>("Thread sampling requires a Windows 8.1+ compilation target");
#endif
#elif defined(__APPLE__)
    result.handle_count = unavailable<std::uint64_t>("Windows handle count is unavailable on macOS");
    proc_taskinfo task{};
    errno = 0;
    if (proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task, sizeof(task)) == sizeof(task))
    {
        result.current_rss_bytes = {task.pti_resident_size, {}};
        result.thread_count = task.pti_threadnum > 0
                                  ? resource_metric<std::uint64_t>{static_cast<std::uint64_t>(task.pti_threadnum), {}}
                                  : unavailable<std::uint64_t>("proc_pidinfo returned an invalid thread count");
    }
    else
    {
        const auto reason = errno != 0 ? system_failure("proc_pidinfo(PROC_PIDTASKINFO)", errno)
                                       : "proc_pidinfo(PROC_PIDTASKINFO) returned an unexpected structure size";
        result.current_rss_bytes = unavailable<std::uint64_t>(reason);
        result.thread_count = unavailable<std::uint64_t>(reason);
    }
    result.fd_count = read_fd_count();
#elif defined(__linux__)
    result.handle_count = unavailable<std::uint64_t>("Windows handle count is unavailable on Linux");
    {
        std::unique_ptr<std::FILE, decltype(&std::fclose)> statm(std::fopen("/proc/self/statm", "r"), &std::fclose);
        const int open_error = errno;
        unsigned long long virtual_pages = 0, resident_pages = 0;
        const auto page_size = sysconf(_SC_PAGESIZE);
        if (!statm)
        {
            result.current_rss_bytes =
                unavailable<std::uint64_t>(system_failure("fopen(/proc/self/statm)", open_error));
        }
        else if (std::fscanf(statm.get(), "%llu %llu", &virtual_pages, &resident_pages) != 2 || page_size <= 0 ||
                 resident_pages > (std::numeric_limits<std::uint64_t>::max)() / static_cast<std::uint64_t>(page_size))
        {
            result.current_rss_bytes = unavailable<std::uint64_t>("Invalid /proc/self/statm RSS or system page size");
        }
        else
        {
            result.current_rss_bytes = {resident_pages * static_cast<std::uint64_t>(page_size), {}};
        }
    }
    {
        std::unique_ptr<std::FILE, decltype(&std::fclose)> status(std::fopen("/proc/self/status", "r"), &std::fclose);
        result.thread_count = unavailable<std::uint64_t>("/proc/self/status has no valid Threads entry");
        if (!status)
        {
            result.thread_count = unavailable<std::uint64_t>(system_failure("fopen(/proc/self/status)", errno));
        }
        else
        {
            char line[512];
            while (std::fgets(line, sizeof(line), status.get()))
            {
                unsigned long long threads = 0;
                if (std::sscanf(line, "Threads: %llu", &threads) == 1 && threads > 0)
                {
                    result.thread_count = {threads, {}};
                    break;
                }
            }
        }
    }
    result.fd_count = read_fd_count();
#else
    result.current_rss_bytes = unavailable<std::uint64_t>("Current RSS is unsupported on this platform");
    result.fd_count = unavailable<std::uint64_t>("File-descriptor count is unsupported on this platform");
    result.handle_count = unavailable<std::uint64_t>("Handle count is unsupported on this platform");
    result.thread_count = unavailable<std::uint64_t>("Thread count is unsupported on this platform");
#endif
}

} // namespace process_resource_detail

// Call sample() from one thread. The individual OS counters are not an atomic snapshot.
// No timer, worker thread, PID selection or modification of system limits is performed.
class process_resource_sampler
{
public:
    process_resources sample() {
        process_resources result;
        const auto before = std::chrono::steady_clock::now();
        const auto cpu = process_resource_detail::read_cpu(result);
        const auto after = std::chrono::steady_clock::now();
        result.monotonic_time = before + (after - before) / 2;
        if (!cpu.value)
        {
            result.cpu_percent = process_resource_detail::unavailable<double>(cpu.unavailable_reason);
            previous_.reset();
        }
        else
        {
            if (previous_)
            {
                result.cpu_percent = process_resource_detail::interval_cpu_percent(previous_->second, *cpu.value,
                    std::chrono::duration<double>(result.monotonic_time - previous_->first).count());
            }
            else
            {
                result.cpu_percent = process_resource_detail::unavailable<double>(
                    "A previous successful CPU sample is required to establish an interval");
            }
            previous_ = std::pair{result.monotonic_time, *cpu.value};
        }
        process_resource_detail::read_memory_and_counts(result);
        return result;
    }

private:
    std::optional<std::pair<std::chrono::steady_clock::time_point, double>> previous_;
};

} // namespace arknet::benchmark
