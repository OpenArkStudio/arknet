# 测试与验证

[本地性能报告](../performance/overview_CN.md)

[UDP 性能报告与缓冲诊断](../performance/udp-update_CN.md) · [缓冲区配置](../guides/socket-buffers_CN.md)

功能测试验证正确性，持续压力测试检查重复工作、重启与清理。
性能测量在这些检查通过后于本机执行，使用不带 Sanitizer 的 Release 构建。
CI 用于回归和冒烟执行，共享 runner 的时间不作为发布的性能基线。

## CI 覆盖范围

源分支 PR 运行格式、文档检查，以及 Linux Boost/TLS Release 下的相关模块回归。
RUDP/ARQ/FEC、UDP、HTTP/WebSocket、MySQL 和 Redis 路径有明确映射；跨模块改动取并集。
公共 TCP、共享头文件、构建、依赖、CI 及未分类路径运行完整的 Linux Boost/TLS Release
测试、示例构建和基准冒烟检查。

包含代码的合并队列候选和手动执行保留完整原生矩阵：Linux/macOS/Windows、
Boost/Standalone Asio、TLS、Linux 无 TLS，以及 Linux AddressSanitizer（内存错误检测）与
UndefinedBehaviorSanitizer（未定义行为检测）组合、ThreadSanitizer（线程数据竞争检测）。
源分支通过不代表已完成全矩阵验收。

严格分类的 README／纯文档改动在两个阶段均运行文档和格式检查。十二个矩阵任务保留必需
检查名称；源分支中不属于计划范围的任务明确记录仅验证范围，不声称执行了原生测试。
改动范围缺失或格式错误时运行完整矩阵；范围验证失败会阻止所有原生检查通过。
重命名同时分类新旧路径，不为新提交取消正在执行的检查。

聚合 gate 迁移的第一阶段新增 `CI - required`，保留已有十四个检查名称、固定十二格矩阵和全部原生命令。
这一阶段不移除只验证范围的执行机任务，也不修改分支保护。gate 使用内置 token，并仅为自己的 job 授予
`contents: read` 与 `actions: read`，读取本轮实际任务及步骤元数据。它按事件中的精确提交范围重新计算计划，
要求范围输出一致、文档与格式检查真实成功，且每个已审定矩阵配置恰好有一个成功任务。
结果缺失、重复、不完整或未知时均失败。

完整验收要求每格真正完成配置、构建和对应测试。源分支模块计划只在 Linux Boost/TLS Release 执行原生检查；
其他格必须成功记录仅验证范围，并跳过原生步骤。纯文档计划要求十二格全部记录仅验证范围。
任务结论成功不能替代被跳过的必需步骤、其他测试配置或合并候选所需的完整验证。
gate 还核对现有计划要求执行的基准与报告检查步骤。

只有实际运行原生检查的格子，才允许明确标为可选的缓存安装、归档恢复、统计步骤及对应 action 的 post 步骤报告 `failure`。
任务本身及全部必需的准备、配置、构建和测试步骤仍须成功。关闭的缓存恢复可跳过；编译缓存安装失败后，
必须跳过其恢复与启动，直接调用编译器。只验证范围的格子必须跳过全部缓存步骤及任何已记录的缓存 post 步骤。
步骤缺失、结果未知或其他步骤失败仍会报错。fixture 只验证这些回退规则，不证明缓存命中、托管验收或耗时改善。

只有这个 gate 已通过真实源分支、纯文档及完整合并候选运行后，后续改动才可考虑用聚合检查替换固定的受保护上下文，
并引入动态的源分支／文档矩阵。分支保护迁移与执行机任务减少属于后续工作；本地 fixture 测试只验证 gate 规则，
不能替代托管环境的原生验收。

本地可以选择已注册的检查名称；省略此选项时运行全部检查，未知或不可用的名称会使配置失败：

```sh
cmake -S . -B build -DARKNET_ENABLE_SSL=ON '-DARKNET_CHECK_TESTS=arq_core;fec'
cmake --build build --target arknet_check --parallel 2
```

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

[日志与复现命令](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec/README_CN.md)
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

CI 在 Linux／macOS／Windows 检查两种依赖方案，Linux 另运行
AddressSanitizer/UndefinedBehaviorSanitizer 和 ThreadSanitizer。
Job 名称包含系统、依赖方案、TLS 和检查模式，保留全部 14 项检查名称。
源分支 PR 修改已映射的模块时，在 Linux/Boost/TLS/Release 执行选中的原生检查；
其他矩阵单元只记录检查范围。包含代码的合并队列候选、手动触发和模块映射之外的改动
执行完整的 12 格原生检查。纯文档候选执行文档和格式检查。
PR 源分支通过，不代表包含最新 main 和前序队列改动的候选已经通过。
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
[构建、运行日志与复现脚本](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261010/usage/README_CN.md)
独立保留这项检查，不混入性能测量。

### 有界编译并行度

已验收的[合并候选运行 39372855018](https://github.com/OpenArkStudio/arknet/actions/runs/39372855018)
对应候选 `36e4e184b6d145c2fd03dbf1e99107b590b518ad`。以下耗时单位为分钟，描述单轮托管执行，
不是受控的性能比较：

| 执行配置 | 任务总耗时 | 配置阶段 | 构建 | 测试 |
| --- | ---: | ---: | ---: | ---: |
| Linux Boost TLS，AddressSanitizer（内存错误检测）+ UndefinedBehaviorSanitizer（未定义行为检测） | 58.35 | 3.42 | 50.68 | 3.45 |
| Linux Standalone TLS，AddressSanitizer（内存错误检测）+ UndefinedBehaviorSanitizer（未定义行为检测） | 33.02 | 2.92 | 27.28 | 2.27 |
| Windows Boost TLS Release | 56.47 | 10.88 | 37.77 | 6.35 |

只有 Linux Boost 的 AddressSanitizer（内存错误检测）与 UndefinedBehaviorSanitizer（未定义行为检测）构建
可以试验性选择两个编译槽。构建开始前，Node helper 要求以下条件同时满足：

- `os.availableParallelism()` 报告至少四个可用 CPU 槽。
- 有效内存上限至少为 15 GiB：取实测 `os.totalmem()` 与正的 `process.constrainedMemory()` 限额中的较小值。
- `process.availableMemory()` 独立报告进程仍有至少 12 GiB 可用内存，且不超过上述有效上限。

受限内存返回零时，记录为 `no-reported-constraint`，不再添加数值上限；此时使用实测总内存，
仍须独立通过可用内存与 CPU 检查。零不能证明内存无限。观测缺失、非法、矛盾或不足时选择一个槽。
其他配置保留现有并行度；源模块目标、sccache launcher、全部十二格矩阵、原生测试和 60 分钟期限保持不变。
required gate 核对规划步骤仅在目标原生格中实际成功执行。

helper 在任务日志和摘要中输出真实观测、阈值、所选并行度与原因。这是保守的调度启发式：
12 GiB 可用内存是两个编译槽及其开销的准入预算，不是实测编译峰值或强制单槽上限。
耗时元数据没有编译器／链接器内存峰值，应用基准 RSS 不能补足这项证据。
合入前必须通过完整托管矩阵；本地检查不证明提速。发生内存耗尽时应保留日志并修正，不能自动重试以掩盖失败。

API 语义参考：[Node 操作系统资源接口](https://nodejs.org/api/os.html#osavailableparallelism)、
[受限内存](https://nodejs.org/api/process.html#processconstrainedmemory)、
[可用内存](https://nodejs.org/api/process.html#processavailablememory)。

### 原生构建缓存

每个实际执行原生检查的矩阵单元使用 Mozilla 官方 sccache action `v0.0.11`
和 sccache `v0.16.0`。CMake 通过两个 compiler launcher 选项传入缓存工具；
编译器、构建类型、编译参数、选择的目标和测试命令保持不变。
缓存工具安装或服务启动失败时，直接调用编译器。服务通信失败时，
`SCCACHE_IGNORE_SERVER_IO_ERROR=1` 使编译回退到本地编译器。

GitHub Actions 分别持久化本地编译缓存和 vcpkg `files` 二进制归档。
每份编译缓存限制为 512 MiB。缓存键包含操作系统、架构、runner 镜像及版本、
目标与宿主 triplet、固定的 vcpkg 版本、manifest／配置／overlay 内容、请求的 features、默认 features 和矩阵构建配置。
Release、AddressSanitizer/UndefinedBehaviorSanitizer 与 ThreadSanitizer 使用独立配置。
vcpkg 复用依赖前还会校验各软件包的 ABI。依赖归档要求缓存键完全匹配；
编译缓存通过配置专属前缀恢复，并使用每次运行的独立后缀保存新增对象。
缓存不包含构建目录或测试结果。

Actions 缓存的可见范围也取决于工作流 ref。运行可以恢复当前 ref 和默认分支中的
匹配缓存。PR merge ref 写入的缓存只供同一个 PR 使用，其他 PR 和 `main` 无法恢复。
合并队列 ref 是临时 ref，写入的缓存也不会进入 `main`。
因此，日常 PR 和队列运行不会更新默认分支的共享缓存。

本改动合入后，使用现有的 **CI** 工作流手动预热共享缓存：选择 **Run workflow**，
将分支设为 **main**，然后启动运行。这个手动 `workflow_dispatch` 执行完整原生矩阵，
并将成功生成的缓存保存在 `main` 下。runner 镜像、依赖或构建配置变化后，需要重新预热；
源码变化后，也可通过再次预热更新编译对象。下次手动预热之前，新的 PR 和队列候选
可能重新编译 main 缓存中尚未包含的对象。
这里不新增 push 或定时触发的矩阵，也不将 PR／队列产物提升为 main 缓存。
详见 [GitHub 缓存范围限制](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache)。

缺少缓存标识或归档目录不可用时，关闭持久化。恢复或保存缓存失败不替代编译和测试。
Job 摘要记录归档恢复状态、依赖归档数量和 `sccache --show-stats`；Configure
日志记录 vcpkg 软件包恢复与构建数量。Actions 归档命中本身不能证明编译或依赖命中。
声明性能改善前，需要在 hosted runner 上比较相同源码版本、相同配置的冷、热缓存运行；
本地契约检查不测量 hosted 性能。现有源分支检查范围和完整矩阵的 12 格检查保持不变。

参考资料：[Mozilla action](https://github.com/Mozilla-Actions/sccache-action/tree/v0.0.11)、
[sccache launcher 与回退](https://github.com/mozilla/sccache/tree/v0.16.0)、
[vcpkg 二进制缓存配置](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching)。
