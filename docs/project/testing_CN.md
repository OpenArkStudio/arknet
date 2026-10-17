# 测试与验证

[本地性能报告](../performance/overview_CN.md)

[UDP 性能报告与缓冲诊断](../performance/udp-update_CN.md) · [缓冲区配置](../guides/socket-buffers_CN.md)

功能测试验证正确性，持续压力测试检查重复工作、重启与清理。
性能测量在这些检查通过后于本机执行，使用不带 Sanitizer 的 Release 构建。
CI 用于回归和冒烟执行，共享 runner 的时间不作为发布的性能基线。

## CI 覆盖范围

代码、依赖、构建、CI 配置及无法分类的改动运行完整原生矩阵：Linux/macOS/Windows、
Boost/Standalone Asio、TLS、Linux 无 TLS，以及 Linux ASan/UBSan 和 TSan。
手动触发 CI 时也运行完整矩阵。

仅修改根目录 README 或 `docs/` 下的 Markdown、MDX、SVG、CSS、图片文件时，
运行文档和格式检查。十二个原生矩阵任务保留必需检查名称，明确记录本次只验证改动范围，
不安装原生依赖、构建或执行 C++ 测试。

PR 和合并队列的完整候选版本使用同一规则。每个改动路径都必须符合文档范围，重命名时
同时检查新旧路径。提交对象缺失、改动列表为空或不可用、事件类型未知时均运行完整检查。
范围检查失败会阻止必需检查通过。分支保护保持不变，也不会为新提交取消正在执行的检查。

下载[验证日志与源码对应关系](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-validation-logs.tar.gz)及 [SHA256 校验和](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-validation-logs.tar.gz.sha256)。归档包含双语说明和独立校验工具。

## 构建与执行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_TESTS=ON \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_USE_BOOST_ASIO=ON
cmake --build build --parallel 3
cmake --build build --target arknet_check
build/tests/arknet_stress_test --protocol all --seconds 60 \
  --clients 16 --rounds 2 --window 4 --certs tests/certs
```

使用 Standalone Asio 时，在独立构建目录设置
`-DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`。
HTTP 和 WebSocket 检查依赖 Boost.Asio 与 Boost.Beast，因此两种配置的检查数量不同。
Windows 可执行文件带 `.exe` 后缀。
仓库提供的 TLS 证书仅用于测试，不用于生产身份。

测试使用 doctest 2.5.3，源文件保存在 `tests/vendor/doctest`，
不会安装，也不成为应用依赖。`arknet_check` 直接执行测试程序、
安装包消费项目和已启用的基准冒烟用例，设置超时，并将日志写入
`build/tests/logs/`，不使用 CTest。

```sh
build/tests/arknet_tcp_test --list-test-cases
build/tests/arknet_tcp_test --test-case="*framing*"
build/tests/arknet_tls_test --reporters=junit --out=build/tls.xml
```

功能测试和性能基准都是包含独立 `main` 的 C++ 程序，编译后直接运行。
这些 C++ 功能测试和单项基准不需要 Python、Ruby、`jq`、GNU `timeout` 或 `shasum`。

## 正确性覆盖

| 测试组 | 内容 |
| --- | --- |
| TCP／UDP／WebSocket | 拆包、二进制／空数据、自定义 Session、端点路由、重连和发送所有权 |
| HTTP／router | 独立 raw 对端的 chunked／HEAD／Expect、有序流水线、上限、方法／路径、中间件、错误和 HTTP/HTTPS 绑定 |
| HTTP 请求 | 独立 raw 对端、HEAD／临时／chunked／EOF 响应、取消、截止时间、上限与完成回调恰好执行一次 |
| ARQ／RUDP | 原版协议互通、丢失／乱序、内存上限、握手重试、IPv4／IPv6、旧 SYN 处理和关闭 |
| FEC／RUDP FEC | 独立 Reed–Solomon 实现互通、协商、CRC 拒绝、重复与重放、恢复、不完整分组、内存预算、固定／自适应策略和并发统计 |
| lifecycle／detail／IO pool | timer／post 取消、实际完成后销毁、mutable 监听器、执行器串行化、排队任务和重启 |
| 进程退出／线程存储 | 回调释放最后一个所有者、嵌套线程池、直接及间接全局所有权、两种跨编译单元链接顺序、退出后拒绝构造及保留显式 Asio 配置 |
| concurrency／keepalive | 发送配额、回调完成计数、并发停止和 TCP keepalive |
| TLS | 信任／身份拒绝、mTLS、截止时间、WSS 和重连时创建新 stream |
| IPv6 | TCP、UDP/session/cast、WS/WSS、HTTP/HTTPS、TLS IP 身份验证和握手 Host |
| MySQL | 报文／认证／命令编解码、TLS 身份验证、查询、预处理、事务、结果上限、取消、有界准入，以及独占连接池的重置与数据库隔离 |
| Redis | RESP2/RESP3、认证、TCP/TLS/mTLS、有序流水线、取消后的回复排空、有界准入、拒绝复制命令，以及独占连接池的重置、认证与数据库隔离 |
| 安装包消费项目 | 依赖方案／TLS 配置传播与已安装公开头文件 |

持续压力测试验证数据与序号、完成计数、连接／服务端重启、未完成写入和队列清理。
每种协议将 `--seconds` 分配给四种配置：库内置调度器、单 context 单线程、
单 context 四线程、四个 context 各一线程，再分给指定重启轮次。因此七种协议使用 `--seconds 60`
时，请求的流量阶段共七分钟，另加启动和收尾。慢接收者用例测试背压，不用于比较吞吐量。

## 本机验证

完整测试的源码快照为 `6c8418e`，其中的网络库改动已纳入性能测量版本
`2aa5bc0`。分开验证的纯测试改动 `25bb9f8` 覆盖下文的分配失败可移植性和
原生连接完成顺序。连接池关闭专项单独标识源码快照 `550fb58`。
[PR #16](https://github.com/OpenArkStudio/arknet/pull/16)
记录候选版本审查与跨平台 CI；本机结果不代替候选版本自身的检查。

验证日期为 2026-10-15 至 16 日。机器为 Mac mini `Mac16,11`、Apple M4 Pro、14 个物理／逻辑核心
（10 个性能核心、4 个能效核心）、48 GiB 内存、macOS 27.0.1（26A434）。
依赖为 Standalone Asio 1.38.2、Boost 1.90.0 和 OpenSSL 3.6.4。
普通构建使用 Apple Clang 21.0.0，Sanitizer 构建使用 LLVM 22.1.7。

### 完整检查

四套构建均使用 Debug，启用 TLS 和测试，关闭示例与基准程序。
全部编译完成后，使用真实数据库服务串行执行检查。

| 依赖／检测工具 | 检查数 | doctest 用例数 | 断言数 | 结果 |
| --- | ---: | ---: | ---: | --- |
| Boost.Asio | 52/52 | 470 | 978,433 | 通过 |
| Standalone Asio | 51/51 | 410 | 576,824 | 通过 |
| Boost.Asio／ASan+UBSan | 52/52 | 470 | 615,937 | 通过 |
| Standalone Asio／TSan | 51/51 | 410 | 333,108 | 通过 |

检查数是 `arknet_check` 执行的程序数，用例数是 doctest 执行的用例数。
断言数为实际执行计数，包含按时长运行的压力用例。
已启用的默认测试组均无失败或环境条件导致的跳过。本轮未启用归档的 Zephyr 互通测试，
其独立证据见下文；默认测试包含独立 ARQ/FEC 实现互通。

每套配置均包含外部 context 停止（15 个用例／375 项断言）、排队连接取消（4／44）、
协议、运行时退出、压力测试和安装包使用检查。
安装包消费项目的子构建不继承 Sanitizer 参数，因此不计为 Sanitizer 覆盖。

### 数据库与停止行为覆盖

四套配置均使用隔离的 MySQL Community 8.4.11 和 Redis 7.4.2 执行可选真实服务用例，
没有跳过数据库用例。MySQL 验证 TLS，Redis 覆盖 TCP、TLS 和 mTLS。
测试包含连接／客户端与连接池操作、永久关闭 `close_and_wait()`、晚到连接、
回调与捕获对象释放、任务提交失败及恢复、取消和重置隔离。
未提供服务环境变量时，真实服务用例会跳过，不会用模拟服务代替。
Redis 协议的性能测量使用 Valkey 8.0.11，与这里的 Redis 7.4.2 互通验证分开记录。

基于 TCP 的端点验证原生操作完成和捕获对象释放；多个所有者同时完整停止的串行化保证
限定于 TCP 服务端及其 HTTP／WebSocket／TLS 派生服务端，不扩大为所有协议的新保证。
外部 context 的所有者须先停止外部提交方，并在排空期间保持执行线程运行；
工作线程中的回调不能阻塞等待自身退出。详见[生命周期](../concepts/lifecycle_CN.md)与
[正式停止及故障用例](https://github.com/OpenArkStudio/arknet/pull/16/files)。
这些结果不构成动态库可安全卸载的验证。

### 补充专项检查

| 范围 | 检查数 | 用例数 | 断言数 | 结果 |
| --- | ---: | ---: | ---: | --- |
| Boost.Asio、关闭 TLS：八个指定目标 | 8/8 | 66 | 1,515 | 通过 |
| Boost.Asio ASan+UBSan：排队连接、HTTP 外部 context 与外部停止重复执行 | 72/72 | 280 | 4,664 | 通过 |

无 TLS 检查覆盖 TCP、外部停止、manual-runtime、排队连接取消，以及四个 Redis
停止／任务提交失败目标；不代表完整无 TLS 矩阵，也不代表 MySQL 可以不依赖 OpenSSL。
72 次 ASan 执行包括 32 次排队连接测试、32 次指定 HTTP 用例及八次外部停止测试。
HTTP 显式过滤器排除的用例不属于环境条件导致的跳过；完整 HTTP 测试组已在上表通过。

纯测试改动的分配失败与连接顺序验证单独记录：

| 依赖／检测工具 | 专项用例数 | 断言数 | 指定原生错误用例执行次数 |
| --- | ---: | ---: | ---: |
| Boost.Asio／ASan+UBSan | 30 | 927 | 32/32 通过 |
| Standalone Asio／ASan+UBSan | 30 | 927 | 32/32 通过 |
| Boost.Asio／TSan | 30 | 926 | 32/32 通过 |
| Standalone Asio／TSan | 30 | 926 | 32/32 通过 |

全部专项用例通过，未出现 Sanitizer 诊断。指定原生错误用例共执行 128 次，
每次检查九项断言；过滤器排除的其他用例不属于环境条件导致的跳过。
这些记录补充完整源码快照的结果，不代表纯测试改动的整套检查结果。
MSVC 专用 CRT 分配路径在 Windows CI 中执行，本机 macOS 验证不覆盖该路径。

### 连接池关闭检查

源码快照 `550fb58` 在两种连接池中原位排空 acquire 等待请求，避免
`unordered_map` 移动构造依赖分配。正式回归覆盖两个排队请求、回调重入
close／cancel／acquire、完成通知恰好执行一次及捕获释放。测试使用拥有者侧关闭守卫，
在致命断言导致退出时先永久关闭连接池，再销毁回调引用的状态。

| 依赖／检测工具 | MySQL 关闭用例／断言 | Redis 关闭用例／断言 |
| --- | ---: | ---: |
| Boost.Asio／ASan+UBSan | 14／1,067 | 19／596 |
| Standalone Asio／ASan+UBSan | 14／1,067 | 19／596 |
| Boost.Asio／TSan | 14／1,067 | 19／595 |
| Standalone Asio／TSan | 14／1,066 | 19／595 |

八次目标程序执行均通过，无失败、跳过或 Sanitizer 诊断。四套配置另通过未改动的
MySQL 归还失败测试（5 个用例／222 项断言）与 Redis 归还失败测试（4／107）。
归还失败检查在仅调整测试关闭守卫之前执行，其测试文件和两个连接池头文件与本快照相同。
这些是专项检查，不是本快照的完整测试运行。

Redis 诊断测试将被检查的异常快照保留到并发观察线程退出。验证归档保留原 Linux TSan、
Windows 故障注入失败记录、源码对应关系和专项日志。连接池修正移除了一处分配边界，
不保证所有子连接关闭路径都能从堆内存耗尽中恢复。性能测量继续对应这项关闭修正之前的
`2aa5bc0`。

本机 macOS ASan 关闭泄漏检测。完整检查的 ASan 还关闭了在线符号化，以避开已定位的
符号化程序挂起问题，内存检测仍然启用。没有新增 Sanitizer 抑制规则或强制预包含参数。
仅沿用原版 KCP 测试编译单元的 `null,object-size` 检测排除项。

### RUDP 互通专项证据

以下专项执行记录于 2026-10-13，源码版本与范围保留在链接记录中；
其中较早的完整检查计数不代表当前项目。

| 检测工具 | 范围 | 用例 | 结果 |
| --- | --- | ---: | --- |
| ASan/UBSan | Standalone Asio：ARQ、独立 ARQ/FEC 实现互通、RUDP/FEC、lifecycle、detail 与 concurrency | 109 | 通过 |
| TSan | Standalone Asio：FEC、独立 FEC 实现互通、RUDP/FEC、lifecycle、detail 与 concurrency | 86 | 通过 |

RUDP FEC 端点测试在两种 Release 依赖方案及独立 ASan/UBSan、TSan 构建中均通过 11 个用例、
2,564 项断言。真实 zephyr/arknet 混合进程测试每种依赖方案均通过 12 组，覆盖重连、ARK 二进制
消息头、4096 字节消息体、ARQ 报文丢失和乱序。中继保留 SYN/ACK/FIN，未验证 FIN 丢失后的可靠关闭。

[日志与复现命令](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261013/rudp-fec/README_CN.md)
记录了源码版本和检查范围。仅用于测试的原版 KCP 源码关闭 `null,object-size` 检测，
arknet 自有代码保留完整检测。性能数据见独立的 [RUDP 报告](../performance/rudp_CN.md)。

<Aside type="note" title="部署容量">

本机正确性、Sanitizer 和回环网络结果不能证明可承载集群 10 万～20 万 PCU。
跨主机负载、资源预算、生产时长耐久以及 ARK 业务负载仍需单独验收。

</Aside>

Sanitizer 单独执行，不混入性能测量。LeakSanitizer 的支持依平台而异；
Apple ASan 通过不等同于 Linux 泄漏检查通过。

### Sanitizer 检查

Sanitizer 在开发构建中插入检测代码，报告测试运行时发现的错误。

| 工具 | 检查范围 |
| --- | --- |
| ASan（AddressSanitizer） | 内存错误，例如越界访问、释放后继续访问和重复释放 |
| TSan（ThreadSanitizer） | 未正确同步的并发内存访问，且至少一次访问是写入 |
| UBSan（UndefinedBehaviorSanitizer） | 部分未定义行为，例如有符号整数溢出、无效移位和未对齐访问 |

ASan/UBSan 与 TSan 使用独立构建。它们会增加运行开销，发布的性能测量关闭这些
工具。测试通过只覆盖本次执行的代码路径和线程调度，不能证明所有错误都不存在。

## CI 与未覆盖范围

CI 在 Linux／macOS／Windows 检查两种依赖方案，Linux 另运行 ASan/UBSan 和 TSan。
Job 名称包含系统、依赖方案、TLS 和检查模式。PR、合并队列候选和手动触发均运行完整
14 项检查。PR 源分支通过，不代表包含最新 main 和前序队列改动的候选已经通过。
候选合入 main 后，不重复运行同一套矩阵。README 徽章展示合并队列运行结果。

运行保留现有的 PR 或 ref 并发组。新运行不会取消同组内正在执行的运行，而是等待并发组。
GitHub 仍可能替换同组较早的待执行运行，因此这个设置不保证每次 push 都按 FIFO 顺序保留，
详见 [GitHub 并发规则](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency)。
候选的必需检查仍须全部通过。文档发布继续使用独立的 main push 工作流。
CI 上传测试日志、执行基准冒烟；性能报告矩阵来自本机测量。

本机 RUDP 测量覆盖随机／突发丢包、延迟、重复与乱序，并包含六组 120 秒持续运行。
这些结果不能预测跨主机容量和广域网延迟。文件描述符耗尽、生产时长耐久及
[ARK 业务验收](production-readiness_CN.md)仍需单独验证。

文档中的 14 个独立客户端、服务端程序也在两种依赖方案上编译、运行通过。每种依赖方案
的七组协议检查了回显内容，全部 28 个进程正常退出。
[构建、运行日志与复现脚本](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/usage/README_CN.md)
独立保留这项检查，不混入性能测量。
