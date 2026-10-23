# Process resource sampling

[简体中文](process_resources_CN.md)

`process_resources.hpp` samples only the calling process. It is a C++20 benchmark helper with no arknet, Asio or third-party runtime dependency. It does not inspect other processes, start workers or change operating-system limits. The existing `resources.hpp` helpers and historical benchmark output keep their original meanings.

## Build and run

Compile the formal probe directly from the repository root on macOS/Linux:

```sh
c++ -std=c++20 -O2 -pthread benchmarks/process_resources_probe.cpp -o process_resources_probe
./process_resources_probe --samples 5 --interval-ms 1000
./process_resources_probe --self-check
```

On Windows, use Visual Studio Developer PowerShell:

```powershell
cl /std:c++20 /EHsc /O2 /DNOMINMAX benchmarks/process_resources_probe.cpp /Fe:process_resources_probe.exe /link psapi.lib kernel32.lib
.\process_resources_probe.exe --samples 5 --interval-ms 1000
.\process_resources_probe.exe --self-check
```

The optional benchmark CMake build also provides `arknet_process_resources_probe`:

```sh
cmake -S . -B build/resources -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF
cmake --build build/resources --target arknet_process_resources_probe --config Release
./build/resources/benchmarks/arknet_process_resources_probe --self-check
```

The top-level CMake configuration uses the project's normal dependency discovery; the direct compile commands require only a compiler and OS SDK. Multi-configuration generators place the executable under `benchmarks/Release/`; Windows adds `.exe`. With `ARKNET_BUILD_TESTS=ON`, the same source supplies `arknet_process_resources_test` and participates in `arknet_check`, including configurations where benchmarks are disabled.

## Metrics

| Field | Meaning |
| --- | --- |
| `cpu_percent` | `100 × Δ(process user CPU + kernel CPU) / Δ(monotonic time)`. One fully occupied logical core is 100%; multiple cores can exceed 100%. The first successful sample establishes the baseline and has no percentage. |
| `current_rss_bytes` | Current OS-reported resident memory, in bytes. Windows reports its working set. This is independent of the historical peak. |
| `peak_rss_bytes` | Process-lifetime peak resident memory / Windows peak working set, in bytes. It is not a peak limited to the sampling interval. |
| `fd_count` | Open POSIX file descriptors. Linux excludes the temporary directory FD used by the sampler itself. Unavailable on Windows. |
| `nofile_soft`, `nofile_hard` | Actual calling-process soft/hard `RLIMIT_NOFILE`, read with `getrlimit` on POSIX. The optional value is a `file_descriptor_limit` with `descriptors` and `unlimited`; JSON writes a finite integer or `"unlimited"` for `RLIM_INFINITY`. Unsupported or failed queries use null and a reason. Not applicable to Windows handles. |
| `handle_count` | Windows process handles, including object types beyond files/sockets. This is not a file-descriptor count and is unavailable on POSIX. |
| `thread_count` | Native threads in the current process, including runtime-owned threads. |
| `monotonic_time` | `steady_clock::time_point` captured at the midpoint of the cumulative CPU query. It is not UTC or a timestamp to compare across machines. The probe emits `elapsed_seconds` relative to its first sample. |

Each metric has an optional `value` and an `unavailable_reason`. A missing value is JSON `null`, never an invented zero. A real zero remains a value. OS errors, unsupported facilities, invalid CPU intervals and a missing CPU baseline explain their absence. The reason is diagnostic text, not a stable machine-readable error identifier.

Descriptor limits include sockets, files, pipes and event-loop descriptors, so they are not a TCP connection limit or a capacity guarantee. A child inherits its launcher's limits; record the benchmark process's own startup values. Another shell's `ulimit`, launch-service defaults and current macOS `kern.maxfiles*` settings cannot establish a historical process's limits and are not portable defaults. The helper never calls `setrlimit` or modifies host settings. New diagnostics do not fill missing historical evidence or explain an unresolved traffic stall.

```cpp
#include <chrono>
#include <iostream>
#include <thread>

#include "process_resources.hpp"

int main() {
    arknet::benchmark::process_resource_sampler sampler;
    sampler.sample();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    auto current = sampler.sample();
    if (current.cpu_percent.value)
    {
        // 100% represents one logical core; do not divide by the CPU count.
        std::cout << *current.cpu_percent.value << "%\n";
    }
}
```

Call each sampler serially from one thread; use separate samplers for independent observers. Individual OS queries do not form an atomic snapshot. Prefer a one-second interval, and avoid intervals below 100 ms: procfs reads, FD enumeration and Windows thread snapshots have overhead; the sampler's own CPU and memory costs are included in the observed process resources. The probe defaults to five samples at one-second intervals; its bounded CLI accepts 1–3600 samples and 100–60000 ms intervals. It has no PID argument.

## Platform details

| Platform | Sources and limits |
| --- | --- |
| Linux | `getrusage(RUSAGE_SELF)` for CPU/peak RSS, `/proc/self/statm` for current RSS, `/proc/self/status` for threads, `/proc/self/fd` for FDs. procfs must be accessible. Linux's fast resident counters are approximate; this helper does not perform the more expensive `smaps` scan. |
| macOS | `getrusage(RUSAGE_SELF)` for CPU/peak RSS and `proc_pidinfo()` for current resident bytes, threads and FDs. FD enumeration uses up to three bounded attempts; concurrent descriptor growth can make a sample unavailable. |
| Windows | `GetProcessTimes`, `GetProcessMemoryInfo` and `GetProcessHandleCount`. Thread count uses a `PSS_CAPTURE_THREADS` snapshot of `GetCurrentProcess()` only when `_WIN32_WINNT >= 0x0603`; lower compilation targets report an unavailable thread count. The header does not change `_WIN32_WINNT` or the library's supported Windows version. Enabling this branch requires the Windows 8.1+ SDK APIs and runtime, with `psapi.lib` and `kernel32.lib`. No system-wide process/thread enumeration is used. |

The finite self-check validates CPU scaling and invalid intervals, explicit unavailable metrics including descriptor limits, repeated sampling without an extra FD, owned temporary-file and worker counts, and current RSS after releasing a 32 MiB mapping. Connection-resource unit tests also cover finite/infinite limits, failed limit queries, startup JSON and unchanged process limits. The self-check then runs about 150 ms of CPU work. Restricted or unsupported counters remain explicitly unavailable; this is tool verification, not a connection-capacity or production acceptance report. JSON Lines and other run output belong outside the source repository.

API references: [Linux getrusage](https://man7.org/linux/man-pages/man2/getrusage.2.html), [Linux resident-counter limitations](https://man7.org/linux/man-pages/man5/proc_pid_statm.5.html), [Apple process-info definitions](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/proc_info.h), [Windows thread snapshots](https://learn.microsoft.com/en-us/windows/win32/api/processsnapshot/nf-processsnapshot-psscapturesnapshot).
