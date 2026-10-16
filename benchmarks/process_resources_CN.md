# 进程资源采样

[English](process_resources.md)

`process_resources.hpp` 只采集当前进程，是不依赖 arknet、Asio 或第三方运行库的 C++20 基准辅助工具。它不会检查其他进程、启动工作线程或修改系统限制。现有 `resources.hpp` 及历史基准输出的指标含义保持不变。

## 编译与运行

在仓库根目录直接编译正式探测程序。macOS/Linux 使用：

```sh
c++ -std=c++20 -O2 -pthread benchmarks/process_resources_probe.cpp -o process_resources_probe
./process_resources_probe --samples 5 --interval-ms 1000
./process_resources_probe --self-check
```

Windows 使用 Visual Studio Developer PowerShell：

```powershell
cl /std:c++20 /EHsc /O2 /DNOMINMAX benchmarks/process_resources_probe.cpp /Fe:process_resources_probe.exe /link psapi.lib kernel32.lib
.\process_resources_probe.exe --samples 5 --interval-ms 1000
.\process_resources_probe.exe --self-check
```

可选的 CMake 基准构建也提供 `arknet_process_resources_probe`：

```sh
cmake -S . -B build/resources -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF
cmake --build build/resources --target arknet_process_resources_probe --config Release
./build/resources/benchmarks/arknet_process_resources_probe --self-check
```

顶层 CMake 配置沿用项目的依赖查找；直接编译只需要编译器和系统 SDK。多配置生成器将程序放在 `benchmarks/Release/` 下，Windows 程序带 `.exe` 后缀。启用 `ARKNET_BUILD_TESTS=ON` 时，同一源码提供 `arknet_process_resources_test` 并纳入 `arknet_check`，包括未启用 benchmarks 的配置。

## 指标

| 字段 | 含义 |
| --- | --- |
| `cpu_percent` | `100 × Δ(进程用户态 CPU + 内核态 CPU) / Δ(单调时间)`。占满一个逻辑核为 100%，使用多个核时可超过 100%。首次成功采样只建立基线，不产生占用比例。 |
| `current_rss_bytes` | 操作系统报告的当前驻留内存，单位为字节；Windows 对应 working set。它独立于历史峰值。 |
| `peak_rss_bytes` | 进程生命周期内的驻留内存峰值，Windows 对应 peak working set，单位为字节。它不是采样区间内的峰值。 |
| `fd_count` | POSIX 已打开的文件描述符数量。Linux 排除采样器自身打开的临时目录 FD；Windows 不提供该指标。 |
| `handle_count` | Windows 进程句柄数量，包含文件和 socket 之外的对象类型。它不等同于 FD 数量，POSIX 不提供该指标。 |
| `thread_count` | 当前进程的原生线程数量，包含运行库线程。 |
| `monotonic_time` | 累计 CPU 查询前后单调时间的中点，类型为 `steady_clock::time_point`。它不是 UTC，不能跨机器比较；探测程序输出相对于首次采样的 `elapsed_seconds`。 |

每个指标包含可选的 `value` 与 `unavailable_reason`。缺失值输出 JSON `null`，不会用零代替；真实的零仍保留为数值。不支持、系统 API 失败、CPU 区间无效或缺少 CPU 基线时，原因明确说明。原因属于诊断文本，不是稳定的机器可读错误标识。

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

每个采样器由单线程串行调用；独立的观察者分别创建采样器。多项系统查询不是原子快照。建议每秒采样一次，避免小于 100 ms 的区间：读取 procfs、枚举 FD 和获取 Windows 线程快照都有开销，采样器自身的 CPU 与内存开销也会计入当前进程的资源消耗。探测程序默认采样五次，间隔一秒；命令行限定 1–3600 次、100–60000 ms 间隔，不提供 PID 参数。

## 平台实现

| 平台 | 数据来源与限制 |
| --- | --- |
| Linux | `getrusage(RUSAGE_SELF)` 提供 CPU／峰值 RSS；`/proc/self/statm` 提供当前 RSS；`/proc/self/status` 提供线程数；`/proc/self/fd` 提供 FD 数量。需要可访问的 procfs。Linux 的快速驻留计数存在近似误差，本工具不进行开销更大的 `smaps` 扫描。 |
| macOS | `getrusage(RUSAGE_SELF)` 提供 CPU／峰值 RSS，`proc_pidinfo()` 提供当前驻留字节数、线程数及 FD 数量。FD 枚举最多尝试三次；并发增长的描述符可能使一次采样不可用。 |
| Windows | 使用 `GetProcessTimes`、`GetProcessMemoryInfo` 与 `GetProcessHandleCount`。仅在 `_WIN32_WINNT >= 0x0603` 时对 `GetCurrentProcess()` 获取 `PSS_CAPTURE_THREADS` 快照；更低编译目标的线程数明确标为不可用。头文件不修改 `_WIN32_WINNT`，也不抬高库的 Windows 版本要求。启用该分支需要 Windows 8.1+ SDK API 和运行环境，并依赖 `psapi.lib` 与 `kernel32.lib`；不会枚举全系统进程或线程。 |

有限自检覆盖 CPU 比例与无效区间、缺失值表达、连续采样不多计临时 FD、自身临时文件与工作线程数量，以及释放 32 MiB 映射后的当前 RSS；随后执行约 150 ms 的 CPU 工作。不支持或受限制的指标仍明确显示不可用。这是工具自检，不是连接容量或生产验收报告。JSON Lines 和其他运行输出应存放在源码仓库之外。

API 参考：[Linux getrusage](https://man7.org/linux/man-pages/man2/getrusage.2.html)、[Linux 驻留计数限制](https://man7.org/linux/man-pages/man5/proc_pid_statm.5.html)、[Apple 进程信息定义](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/proc_info.h)、[Windows 线程快照](https://learn.microsoft.com/en-us/windows/win32/api/processsnapshot/nf-processsnapshot-psscapturesnapshot)。
