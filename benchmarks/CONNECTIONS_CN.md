# TCP 连接负载

简体中文 · [English](CONNECTIONS.md)

`arknet_connection_benchmark` 将 TCP 服务端与客户端放在独立进程中运行，用于验证建连、空闲驻留、闭环 Echo 流量、资源限制和停止清理，为部署容量测试准备工具。小规模回环测试通过，不代表已经达到某个 PCU 或生产 SLO。

程序使用 arknet 公开的 `tcp_server`、`tcp_client` 和 `io_pool`，无需 Python、Ruby、数据库或额外运行服务。当前覆盖 TCP 回调接口、数字 IPv4/IPv6 地址及 `idle`、`traffic` 两种场景。TLS、重连负载、固定请求到达率及真实 ark 应用消息属于独立工作。

## 构建

CMake 可以找到 Boost 1.90 或更新版本时：

```sh
cmake -S . -B build/connections -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=OFF
cmake --build build/connections --target arknet_connection_benchmark --config Release
```

可执行文件位于 `build/connections/benchmarks/` 下；多配置生成器可能再增加 `Release/` 目录。Windows 使用 Visual Studio 2022 开发者终端。arknet 原有的依赖及工具链参数同样适用。

使用 Standalone Asio 1.38 或更新版本时，额外设置 `-DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`。程序只引用 TCP 头文件，不需要 Boost.Beast。

也可以直接编译。例如在已安装 Boost 头文件的 Linux 或 macOS 上：

```sh
c++ -std=c++20 -O3 -DNDEBUG -pthread -DARKNET_USE_BOOST_ASIO \
  -Iinclude -I/path/to/boost benchmarks/connections.cpp -o connection-load
```

Windows 下由 CMake 提供系统网络与进程内存库。构建或使用 arknet 无需运行性能负载。

## 分别运行服务端与客户端

在第一个终端启动服务端，显式指定总连接目标：

```sh
./build/connections/benchmarks/arknet_connection_benchmark \
  --role server --scenario idle --listen 127.0.0.1 --port 19090 \
  --run-id 9001 --expected-connections 100 --max-connections 100 \
  --io-threads 2 --deadline-seconds 60
```

在另一个终端运行：

```sh
./build/connections/benchmarks/arknet_connection_benchmark \
  --role client --scenario idle --target 127.0.0.1 --port 19090 \
  --run-id 9001 --client-id 1 --connections 100 --max-connections 100 \
  --connect-rate 100 --connect-parallel 16 --hold-seconds 10 \
  --io-threads 2 --deadline-seconds 45
```

将两端都改成 `--scenario traffic` 即可发送 Echo 请求。客户端设置 `--payload 1024 --window 4` 时，每连接最多有四条在途请求。两端必须使用相同的场景、运行标识和包体大小。`--payload` 指正文大小；每条测试消息另带 64 字节控制头及 arknet 数据帧长度前缀。

测试 IPv6 回环时，将两端地址都替换成 `::1`。跨机器测试时，客户端目标填写服务端网卡的数字 IP 地址；域名解析不计入本工具的测量范围。服务端可以监听通配地址，客户端必须填写具体目标地址。服务端支持 `--port 0`，会在第一条 `ready` JSON 记录中输出实际端口。

使用多个客户端进程时，服务端的 `--expected-connections` 和 `--max-connections` 应设为所有客户端目标的总和。为每个客户端进程分配不同的非零 `--client-id`，跨机器也不能重复。默认客户端标识为本进程 PID，跨机器并不保证唯一。连接标识在每个客户端进程内从 1 开始。服务端拒绝本次运行中重复的客户端与连接标识组合。

## 阶段与成功条件

1. 客户端按 `--connect-rate` 上限发起连接，同时最多保留 `--connect-parallel` 个未完成的 TCP 建连请求。该速率是上限，不保证机器实际达到；调度和机器容量可能降低实际速率。
2. 每条连接交换 `HELLO` 并确认身份。`established` 统计 TCP 建连成功，`ready` 统计测试应用握手完成。这些是测试控制消息，不是 ark 业务鉴权。
3. 该客户端进程的全部连接就绪后，交换 `START` 确认，再开始驻留阶段。`idle` 在驻留期间不发送应用心跳或业务数据，操作系统及库的 TCP keepalive 行为另计。
4. `traffic` 为每条连接填充固定请求窗口，收到经过校验的 Echo 响应后再补发请求。这是闭环负载；接收端变慢时，不宣称仍维持固定请求到达率。
5. 驻留结束后停止新增请求，排空 Echo 响应，交换 `FIN` 确认，再关闭 socket。两端都等待端点停止后，才停止 IO 池并等待工作线程退出。

服务端的 `--expected-connections` 独立于某个客户端的 `--connections`。显式目标不为零时，必须有对应数量的接纳、就绪、启动、完成和断连，活动会话及待发送消息都归零，才能返回成功；满足条件后可以提前退出。默认目标为 0 时，只观测到服务端期限，返回 `observation_complete`，不代表通过连接数目标验收。`active_before_stop` 保留观测结束、清理开始前的在线连接数。

每个客户端进程都有独立的驻留起点。当前程序不负责同步多个客户端进程或多台机器的测量阶段，也不重试失败连接或重放请求。

## 参数与限制

| 参数 | 默认值 | 含义与范围 |
| --- | --- | --- |
| `--run-id` | 必填 | 两端共用的非零 64 位无符号运行标识，在 JSON 中以十进制字符串输出。 |
| `--connections` | 1 | 客户端目标，范围为 1–`max-connections`。 |
| `--expected-connections` | 0 | 服务端显式总目标；0 表示只观测，不得超过 `max-connections`。 |
| `--max-connections` | 4096 | 客户端目标或服务端本次运行累计接纳上限，最大 200,000；此上限不是实测容量。关闭连接后不释放额外接纳名额。 |
| `--connect-rate` | 100/s | 客户端连接发起速率上限，1–1,000,000/s；调度延迟后不会突发补发。 |
| `--connect-parallel` | 32 | 未完成的客户端 TCP 建连数量上限，1–4096；不限制已经连接、仍等待 `HELLO` 的会话。 |
| `--connect-timeout-ms` | 5000 | 每次 TCP 建连期限，1–3,600,000 ms。 |
| `--request-timeout-ms` | 5000 | 客户端 `HELLO`、`START`、最早在途 Echo 或 `FIN` 的响应期限，1–3,600,000 ms；计划扫描间隔最多为 50 ms，操作系统调度或输出阻塞还可能继续推迟执行。 |
| `--hold-seconds` | 1 | 正数，客户端驻留时长，必须短于其总期限。 |
| `--deadline-seconds` | 30 | 正数，总运行期限，最多 86,400 s，包含建连、握手、驻留和协议排空；服务端从监听前开始计时。 |
| `--cleanup-seconds` | 5 | 额外的端点和 IO 池清理宽限期，1–60 s。 |
| `--io-threads` | 1 | 库的 IO 工作线程数，1–64。IO 池创建 **N 个 `io_context`，每个由一个工作线程运行**，不是一个 context 由 N 个线程运行。 |
| `--payload` | 1024 | Echo 正文大小，1 B–1 MiB，不含头部；两端保持一致。 |
| `--window` | 1 | 每连接闭环请求窗口，1–1024，仅客户端 `traffic` 使用。 |
| `--max-buffer-bytes` | 4096 | 每个端点的接收缓冲上限，至少为 `payload + 64 + 9`，最多 16 MiB；大包需要显式增加。 |
| `--max-inflight-bytes` | 64 MiB | 全局待发送正文与控制头的字节上限，最大 1 GiB；客户端请求窗口也必须放得下。超过限制会使本次运行失败。 |
| `--buffer-budget-bytes` | 512 MiB | 声明的接收缓冲上限、全局待发送容量及客户端时间戳环的预检预算，最大 16 GiB。 |
| `--sample-ms` | 1000 | 资源及进度输出间隔，100–60,000 ms。 |

缓冲预检量为 `N * max-buffer-bytes + max-inflight-bytes + 时间戳环存储量`，其中 `N` 是客户端目标或服务端接纳上限。它约束声明的消息缓冲预算，**不等于 RSS 上限**。端点对象、分配器开销、库的队列与控制结构、IO 运行时和系统 socket 缓冲还需要额外内存。增加连接数之前应检查进程与系统限额；程序不会调整 FD 限额或系统 socket 设置。

除指定的 IO 工作线程外，每进程还有一条主调度线程和一条清理监控线程，不会为每条连接创建线程。Ctrl+C 或 SIGTERM 会请求失败退出，并执行正常清理。如果总期限加清理宽限期后，运行、最终输出或清理仍未完成，监控线程请求操作系统立即终止进程：Windows 使用 `TerminateProcess`，退出码为 124；POSIX 使用 `SIGKILL`。POSIX shell 通常将信号 9 显示为 137，但这不是跨平台统一退出码的保证。该路径表示 `clean_shutdown=false`，不产生完整成功摘要；终止过程不依赖再写日志、C 运行时的 flush 或 sanitizer 退出处理。

## 输出与指标

Stdout 使用 JSON Lines：服务端的 `ready`、定期 `sample` 和最终 `result`。Stderr 用于参数或启动错误。清理超时中止可能留下不完整输出，因此还需记录进程退出码。分别保存各进程输出，原始结果放在 Git 仓库外。

| 字段 | 口径 |
| --- | --- |
| `attempted`、`established`、`connect_failures` | 客户端 TCP 尝试与结果。建连失败后停止新增，因此失败运行可能没有发起全部目标连接。 |
| `accepted`、`admitted`、`ready`、`started`、`finished`、`disconnected` | 服务端实际 native accept、通过容量限制接纳的连接及测试协议各阶段；客户端字段表示对应的本地阶段。 |
| `active_before_stop`、`active`、`peak_active` | 观测结束时、清理完成后的在线数量，以及本进程峰值。 |
| `data_sent`、`data_received`、`outstanding_requests` | Echo 数据消息计数，不包含控制消息；成功流量运行会排空全部请求。 |
| `traffic_and_drain_elapsed_s` | 客户端从驻留起点到最后一个 `FIN` 确认；服务端从首个 `START` 到最后一个成功发送的 `FIN` 确认。两者范围不同，也不是跨机器同步窗口。 |
| `echo_messages_per_second`、`echo_payload_bytes_per_second` | 接收 Echo 消息数或正文字节数除以本角色的流量与排空时长，包含排空；服务端还包含各客户端起点差异。不含测试头及 TCP/IP 线速开销。 |
| `rtt_p50_upper_us`、`rtt_p95_upper_us`、`rtt_p99_upper_us` | 客户端 Echo 往返延迟桶的上界，单位 µs，包含内容生成与校验、队列和网络传输。没有 Echo 时为 null；不测单向或真实业务延迟。 |
| `rtt_buckets`、`latency_observations` | 用 513 个固定计数器保留全部 RTT 的分布，存储量不随请求数增长。 |
| `cpu_percent`、`peak_rss_bytes` | 本进程全部线程的用户态与系统态 CPU，以及进程生命周期 RSS 高水位。CPU 100% 表示一个逻辑核，可以超过 100%；sample CPU 对应上个采样区间，最终 CPU 对应包含建连和清理的整次运行，不包含另一进程。系统指标不可用时为 null。 |
| `current_rss_bytes`、`fd_count`、`handle_count`、`thread_count` | 本进程当前驻留内存（Windows 工作集）、POSIX 文件描述符、Windows 句柄和原生线程数。文件描述符与句柄是不同指标，不支持的指标为 null。 |
| `resource_sample_elapsed_s`、`resource_cpu_interval_s` | CPU 查询中点相对本次运行开始的秒数，以及 CPU 占用率实际使用的区间长度。CPU 占用率不可用时，区间为 null；这些值不能作为跨机器时间戳。 |
| `resource_phase` | 周期记录为 `running`，可能处于建连、保持或协议排空阶段；最终记录为 `after_cleanup`，在端点和 IO 工作线程停止后读取。最终内存和数量指标不是负载阶段的值。 |
| `resource_unavailable` | 以缺失资源字段名为键、诊断原因为值的对象。真实零值仍是数字；原因文本用于诊断，不是稳定错误码。 |
| `sample_interval_ms` | 最终结果中的计划采样间隔。实际 CPU 采样区间单独报告。 |
| `content_errors`、`sequence_errors`、`send_errors`、`timeouts`、`resource_rejections` | 内容、序号、发送、期限或预算失败；停止过程中取消的发送单独记为 `shutdown_cancelled_sends`。 |
| `clean_shutdown`、`outcome`、`first_error` | 清理状态，以及 `target_complete`、`observation_complete` 或 `failed`。清理完成本身不代表交付成功或达到连接目标。 |

第 0 个延迟桶表示 0 µs；其余桶将每个二次幂区间分成八份，精确索引与包含边界见 `connections_protocol.hpp`。桶宽最多为对应二次幂基数的 12.5%，低数值处保留 1 µs 整数精度。相同定义的桶计数可以相加；各进程 p99 不能相加或平均为全局 p99。只合并同一协调运行及相应测量阶段的结果；程序不保存原始逐条 RTT。

正常退出码：0 表示完成目标或观测且清理成功；1 表示运行、校验或期限失败；2 表示参数不合法或启动异常。清理监控终止在 Windows 下为退出码 124，在 POSIX 下为信号 9；原生进程管理器应检查信号状态，shell 通常显示为 137。失败记录用于诊断，不能静默丢弃，或反复运行覆盖后仅保留吞吐较好的结果。

资源指标由独立的周期与全程采样器收集，均在主线程串行调用。CPU 使用实际单调时钟查询时间计算，不用计划间隔或采样百分比的算术平均值代替。默认间隔仍为一秒；文件描述符枚举和系统线程查询有开销，计入被测进程。系统查询与连接计数不是原子快照。短于采样间隔的运行可以没有周期记录，同时仍有有效的全程 CPU 占用率。最终 RSS、文件描述符、句柄和线程数明确属于清理后阶段，负载期间的值应查看周期记录。不支持的系统指标仍为 null，包括编译目标不支持相关 API 时的 Windows 线程数。采样不会修改系统限制，也不会设置 RSS 上限。

JSON 只新增字段，原有字段的类型和测量阶段保持不变。对比运行时应记录源码、可执行文件身份和 `sample_interval_ms`；资源收集开销与更精确的 CPU 时间戳意味着新旧结果不会完全相同。其他基准程序的输出保持原样。

## 小规模正确性检查

配置 `-DARKNET_BUILD_TESTS=ON` 后可以构建 `arknet_connection_protocol_test`，检查身份保真、损坏与截断消息拒绝、长度溢出及可合并延迟桶边界。`arknet_check` 包含这些检查及 `--help`；POSIX 的满输出管道进程用例会检查监控线程在无人读取输出时仍可终止。部署负载仍在本地手动运行。

`arknet_connection_resources_test` 检查实际资源输出字段、真实零值与缺失指标及其原因、诊断文本转义，以及周期和全程时间范围的独立性。系统计数器本身由独立的[进程资源检查程序](process_resources_CN.md)验证。

扩大规模之前，先用少量连接检查 idle、traffic、IPv4 和 IPv6 的双端最终计数，再验证拒绝连接、错误运行标识、目标期限、重复身份及非法缓冲或窗口预算均非零退出。每次测量保留命令、可执行文件与源码身份、机器配置、全部失败和双端日志。本工具的测试协议不能替代 ark 应用、鉴权、重连恢复或跨机器生产验收。
