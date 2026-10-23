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

严格分类的 README／纯文档改动在两个阶段均运行文档和格式检查。七个原生分组任务保留全部十二种配置；
源分支中不属于计划范围的配置明确记录仅验证范围，不声称执行了原生测试。
改动范围缺失或格式错误时运行完整矩阵；范围验证失败会阻止所有原生检查通过。
重命名同时分类新旧路径，不为新提交取消正在执行的检查。

文档路径包括 `AGENTS.md`、Markdown 和文档资源，以及明确映射的 Nimbus 站点配置、依赖、
Astro/TypeScript 组件、页面与内容工具。规则不会整体豁免 `docs/site/**`：
CI 范围和工作流测试、原生版本与报告检查、C++/CMake 文件、未知脚本及未映射文件类型
仍保留原生检查。混合改动在两个阶段均采用原生检查计划。

工作流定义七个原生分组、文档、格式和 `CI - required`，保护迁移后共十个检查上下文。
现有十五项保护规则属于独立的部署状态；修改工作流源码不会迁移保护。
gate 使用内置 token，并仅为自己的 job 授予 `contents: read` 和 `actions: read`，
读取本轮实际任务及步骤元数据，按事件中的精确提交范围重新计算计划。
它要求范围输出一致、文档与格式检查真实成功、每组恰好有一个成功任务，
并逐项核对全部十二种配置的步骤结论。结果缺失、重复、不完整或未知时均失败。

必需任务通过 GitHub CLI 读取明确的 `GITHUB_RUN_ID` 和 `GITHUB_RUN_ATTEMPT`，
使用 `gh run view --attempt`，不使用默认运行快照。采集器先核对返回的运行 ID 和 attempt，
再使用任务数据。任务整体成功时，步骤元数据仍可能暂时显示待执行或执行中。
只有结构有效但尚未终结的快照，才会触发第二次完整快照读取；该读取以快照中一个实际待终结前置任务为锚点：
`gh run view <run-id> --repo <repository> --attempt <attempt> --job <job-id> --json databaseId,attempt,jobs`。
锚点必须来自同一个快照，不能使用汇总任务或其他运行的任务。
各轮重试在已验证名称／ID 集合内，轮流选择当前快照中的待终结前置任务。
只保留选择顺序，不合并不同锚定快照中的成功状态。
两类读取均核对运行 ID 和 attempt；各次读取的九个前置任务名称／ID 必须保持一致，
每份新快照都独立检查。已完成任务须已包含全部必需具名步骤；
已知的必需步骤失败或格式错误／不完整快照，不能通过刷新变成成功。
明确可选的原生缓存失败仍按原有规则处理。

普通读取和任务锚定读取均待终结时，才按 1、2、4、8 秒，之后 15 秒的间隔退避重试。
120 秒总预算包含每次 CLI 耗时；每次调用最多 20 秒，并受剩余总预算限制。
摘要中的快照数量按真实 CLI 读取次数计数，包括锚定读取。必需任务保留现有五分钟期限。

当前正在执行的 `CI - required` 不参与前置任务就绪判断，但其身份仍须唯一。
采集器将真实终结快照交给同一个严格覆盖验证器，通过后才保存；后续 gate 步骤再次验证。
采集器不把待执行步骤改写为成功。CLI 错误、格式错误或不匹配的身份、缺失／重复／非预期任务、
前置任务失败和最终覆盖不完整均保持失败。部分重跑如果没有本 attempt 的全部前置任务，
不能借用此前 attempt 的成功任务；验收应启动完整新运行。
任务锚点不会将验证范围缩小为单个任务：完整运行返回的整份快照仍须包含九个前置任务和十二种实际原生配置。
各次读取和 attempt 之间不合并快照，也不回退到默认 attempt。

采集修复须在新 head 上通过一次完整新运行。即使稍后读取到原生前置任务成功，
旧手动运行已经失败的汇总 gate 仍保留失败；只重跑旧 gate 不能构成新的全绿验收。
本地 fake CLI 与时钟 fixture 只证明采集和拒绝规则，不证明托管缓存状态或合入提速。

完整验收要求每种配置真正完成独立的配置、构建和测试步骤。
源分支计划只在 Linux Boost/TLS Release 执行原生检查；其他配置记录仅验证范围并跳过原生步骤。
纯文档计划跳过全部原生步骤。任务结论成功不能替代被跳过的必需步骤、其他检测配置，
也不能代替合并候选所需的完整验证。完整套件计划保留 Linux TLS Release 下两种依赖配置的基准与报告检查。

只有实际运行原生检查的分组，才允许明确标为可选的缓存安装、归档恢复、统计步骤及对应 action 的 post 步骤报告 `failure`。
分组本身及必需的准备、配置、构建和测试步骤仍须成功。编译缓存安装失败后，必须跳过恢复与启动并直接调用编译器。
只验证范围的分组必须跳过全部缓存步骤及任何已记录的缓存 post 步骤。
步骤缺失、结果未知或其他步骤失败仍会报错。本地 fixture 只验证规则，不证明托管验收或耗时改善。

### 原生任务分组与保护迁移

Release 任务在有界分组内共用准备步骤。Linux 按 Boost 和 Standalone Asio 分成两个任务，
每组验证 TLS／无 TLS；Windows 两种 TLS 依赖配置分别保留独立任务。
macOS 将两种 TLS 依赖配置合并。Linux AddressSanitizer（内存错误检测）与
UndefinedBehaviorSanitizer（未定义行为检测）组合、ThreadSanitizer（线程数据竞争检测）仍使用独立任务，
各自依次验证 Boost 和 Standalone Asio 的 TLS 配置。七个任务保留原有十二种配置，不增添操作系统或 TLS／检测组合。

每种配置使用独立的 `build/ci-<boost>-<ssl>-<sanitize>` 构建目录，原始阶段日志位于对应 `ci-logs/`，
并保留具名的配置、构建和测试结论。阶段日志只保留在 runner，不上传为附件；
现有测试输出附件仍覆盖 `tests/logs/**/*.log`。Actions 控制台输出与步骤摘要继续用于诊断阶段失败。
前一种配置失败后，后续配置仍执行；失败配置内部跳过不满足前置条件的步骤。
任何必需配置缺失、失败或被跳过都会使分组和汇总 gate 失败。分组超时可能阻止后续配置运行，仍属于验收失败。

只共用准备步骤与缓存容器。全部配置继续请求相同的 `boost;ssl` 依赖 feature 超集。
缓存键保留操作系统、架构、runner 镜像、编译器环境、依赖身份及 Release／检测分组，
并包含分组采用的 Boost、Standalone 或组合依赖标识。`grouped-v2` 将七组命名空间与此前缓存布局隔离。sccache 在共享容器内通过编译命令哈希区分 Boost／TLS 参数。
不缓存构建目录或测试结果；vcpkg 仍校验依赖 ABI。同一 runner 不并发运行原生配置：
Release 使用三个构建槽，Windows Boost 可按下文的资源准入条件使用四个槽。
ThreadSanitizer 保留两个，AddressSanitizer／UndefinedBehaviorSanitizer
保留一个槽，或在原有资源准入条件满足时使用两个槽；每种依赖构建前重新观测资源。
每个分组保留 60 分钟任务期限，没有通过延长期限让串行执行通过。

Windows 的 Ninja 构建通过单独的链接任务池，串行执行可执行文件链接及其后的 DLL 部署。
C++ 编译保留原有并行度，vcpkg 部署与全部测试继续启用，避免这些构建任务同时复制共用的运行库文件。
外部进程仍可能锁定文件；命令失败时直接报告，不自动重试。

下表将原有十二个原生必需检查映射到七个新上下文。各行列出精确的旧名称，包括 TLS 模式对或依赖配置对。
`Documentation checks`、`Style - clang-format 18` 和 `CI - required` 保持名称。

| 原有原生上下文（前缀为 `Test - `） | 新上下文 |
| --- | --- |
| `ubuntu-latest - Boost - TLS - Release`；`ubuntu-latest - Boost - NoTLS - Release` | `Native - ubuntu-latest - Boost - Release` |
| `ubuntu-latest - Standalone - TLS - Release`；`ubuntu-latest - Standalone - NoTLS - Release` | `Native - ubuntu-latest - Standalone - Release` |
| `macos-latest - Boost - TLS - Release`；`macos-latest - Standalone - TLS - Release` | `Native - macos-latest - Release` |
| `windows-latest - Boost - TLS - Release` | `Native - windows-latest - Boost - Release` |
| `windows-latest - Standalone - TLS - Release` | `Native - windows-latest - Standalone - Release` |
| `ubuntu-latest - Boost - TLS - ASan+UBSan`；`ubuntu-latest - Standalone - TLS - ASan+UBSan` | `Native - ubuntu-latest - ASan+UBSan` |
| `ubuntu-latest - Boost - TLS - TSan`；`ubuntu-latest - Standalone - TLS - TSan` | `Native - ubuntu-latest - TSan` |

保护迁移由维护者分阶段验收。先保留原有十五项要求，对新分支的精确 head 执行 `workflow_dispatch`。
修改保护前，验证新分组的完整覆盖：冷缓存与已有缓存运行、十二种配置结论、源分支模块和文档范围以及失败传播。
更早的发版候选仍在验收时，不修改已部署矩阵或保护。新分组证据验收后，
用七个映射后的原生检查替换十二个旧检查，全程保留文档、格式与汇总 gate，
随后要求包含最新 main 的实际合并候选重新完成全量检查。不能为了获得首次绿色 PR 而先删除旧要求。
记录精确版本、运行记录及保护规则前后状态；手动运行通过不能替代最终合并候选验收。
使用真实的分组检查上下文，不创建伪装原生检查的兼容上下文。
只修改工作流源码，不会迁移仓库的分支保护要求。

[运行 39770711811](https://github.com/OpenArkStudio/arknet/actions/runs/39770711811) 的原矩阵实测基线
对应候选 `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`。本地耗时快照记录原有独立配置，
尚未逐项核对缓存命中。以下分钟数只是将已记录阶段重新分组用于规划，
不是新分组任务实测，也不包含准备步骤及其他任务开销：

| 计划分组 | Configure | Build | Test | 已记录阶段合计 |
| --- | ---: | ---: | ---: | ---: |
| Linux Boost Release，TLS + NoTLS | 6.28 | 37.03 | 6.85 | 50.16 |
| Linux Standalone Release，TLS + NoTLS | 5.45 | 15.20 | 4.38 | 25.03 |
| Windows Boost TLS Release | 9.33 | 33.22 | 6.45 | 49.00 |
| Windows Standalone TLS Release | 11.25 | 20.45 | 4.03 | 35.73 |
| macOS Release，两种 TLS 依赖 | 7.55 | 14.67 | 5.57 | 27.79 |
| Linux AddressSanitizer + UndefinedBehaviorSanitizer，两种 TLS 依赖 | 6.40 | 40.35 | 6.00 | 52.75 |
| Linux ThreadSanitizer，两种 TLS 依赖 | 6.08 | 21.81 | 6.35 | 34.24 |

原先五组串行模型合并 Linux Release 的全部四种配置，仅 Build + Test 就达到 63.46 分钟，
尚未计入 Configure 和 Setup。Windows 两种配置的已记录阶段合计为 84.73 分钟。
该模型在本基线上超过不变的 60 分钟期限，
因此 Linux Release 改为按依赖分组，Windows 两种 TLS 依赖分别保留独立任务。

七组方案减少部分重复准备步骤，但阶段求和不构成耗时上限或验收。
AddressSanitizer／UndefinedBehaviorSanitizer 组仅已记录阶段就合计 52.75 分钟，尚未计入准备，仍须真实分组验证。
共用缓存与准备步骤可能减少总 runner 分钟数，串行工作也可能增加最长任务或排队至 gate 完成的等待。
源分支只验证范围的成功不能测量完整覆盖。托管冷缓存与已有缓存运行须在同一版本、全部操作系统上
比较总 runner 分钟数、最长分组、排队至 gate 完成耗时及逐配置耗时，不推断本基线的缓存状态。
60 分钟超时属于覆盖失败，不能作为延长期限的依据。

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

原生连接对照程序可通过 `--operation-trace on --socket-operation-trace on` 记录底层流 API 操作，
两个选项均默认关闭。`connection_socket_operation_trace` 使用确定性模拟流验证观察器，
不使用 socket 或负载进程对，覆盖部分写入、已有缓冲与零字节读取、原处理器关联、取消和冻结。
它与 `connection_operation_trace` 共同验证两套事件环的存储上限及缺失历史的明确报告。
这些检查不能证明报文交付、reactor 就绪事件、尚未确定的 5,000 条连接停顿根因或容量。
用法见[连接工具诊断](../../benchmarks/CONNECTIONS_CN.md)。

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
| 安装包消费项目 | 依赖方案／TLS、公开头文件和可选 C++ compiler launcher 转发；空 launcher、带空格路径、launcher 参数列表与真实编译错误传播 |
| 连接负载进程 | 原始 TCP 对端拆分帧前缀／消息体，并跨 4 KiB 接收容量合并八个请求，分别对接 arknet 与原生 Asio 服务端；检查按序逐字节回复和 FIN，不代表高并发或 TCP 丢包验收 |

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
| Boost.Asio／AddressSanitizer+UndefinedBehaviorSanitizer | 52/52 | 470 | 615,937 | 通过 |
| Standalone Asio／ThreadSanitizer | 51/51 | 410 | 333,108 | 通过 |

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
| Boost.Asio AddressSanitizer+UndefinedBehaviorSanitizer：排队连接、HTTP 外部 context 与外部停止重复执行 | 72/72 | 280 | 4,664 | 通过 |

无 TLS 检查覆盖 TCP、外部停止、manual-runtime、排队连接取消，以及四个 Redis
停止／任务提交失败目标；不代表完整无 TLS 矩阵，也不代表 MySQL 可以不依赖 OpenSSL。
72 次 AddressSanitizer 执行包括 32 次排队连接测试、32 次指定 HTTP 用例及八次外部停止测试。
HTTP 显式过滤器排除的用例不属于环境条件导致的跳过；完整 HTTP 测试组已在上表通过。

纯测试改动的分配失败与连接顺序验证单独记录：

| 依赖／检测工具 | 专项用例数 | 断言数 | 指定原生错误用例执行次数 |
| --- | ---: | ---: | ---: |
| Boost.Asio／AddressSanitizer+UndefinedBehaviorSanitizer | 30 | 927 | 32/32 通过 |
| Standalone Asio／AddressSanitizer+UndefinedBehaviorSanitizer | 30 | 927 | 32/32 通过 |
| Boost.Asio／ThreadSanitizer | 30 | 926 | 32/32 通过 |
| Standalone Asio／ThreadSanitizer | 30 | 926 | 32/32 通过 |

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
| Boost.Asio／AddressSanitizer+UndefinedBehaviorSanitizer | 14／1,067 | 19／596 |
| Standalone Asio／AddressSanitizer+UndefinedBehaviorSanitizer | 14／1,067 | 19／596 |
| Boost.Asio／ThreadSanitizer | 14／1,067 | 19／595 |
| Standalone Asio／ThreadSanitizer | 14／1,066 | 19／595 |

八次目标程序执行均通过，无失败、跳过或 Sanitizer 诊断。四套配置另通过未改动的
MySQL 归还失败测试（5 个用例／222 项断言）与 Redis 归还失败测试（4／107）。
归还失败检查在仅调整测试关闭守卫之前执行，其测试文件和两个连接池头文件与本快照相同。
这些是专项检查，不是本快照的完整测试运行。

Redis 诊断测试将被检查的异常快照保留到并发观察线程退出。验证归档保留原 Linux ThreadSanitizer、
Windows 故障注入失败记录、源码对应关系和专项日志。连接池修正移除了一处分配边界，
不保证所有子连接关闭路径都能从堆内存耗尽中恢复。性能测量继续对应这项关闭修正之前的
`2aa5bc0`。

本机 macOS AddressSanitizer 关闭泄漏检测。完整检查的 AddressSanitizer 还关闭了在线符号化，以避开已定位的
符号化程序挂起问题，内存检测仍然启用。没有新增 Sanitizer 抑制规则或强制预包含参数。
仅沿用原版 KCP 测试编译单元的 `null,object-size` 检测排除项。

### RUDP 互通专项证据

以下专项执行记录于 2026-10-13，源码版本与范围保留在链接记录中；
其中较早的完整检查计数不代表当前项目。

| 检测工具 | 范围 | 用例 | 结果 |
| --- | --- | ---: | --- |
| AddressSanitizer/UndefinedBehaviorSanitizer | Standalone Asio：ARQ、独立 ARQ/FEC 实现互通、RUDP/FEC、lifecycle、detail 与 concurrency | 109 | 通过 |
| ThreadSanitizer | Standalone Asio：FEC、独立 FEC 实现互通、RUDP/FEC、lifecycle、detail 与 concurrency | 86 | 通过 |

RUDP FEC 端点测试在两种 Release 依赖方案及独立 AddressSanitizer/UndefinedBehaviorSanitizer、ThreadSanitizer 构建中均通过 11 个用例、
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
Apple AddressSanitizer 通过不等同于 Linux 泄漏检查通过。

### Sanitizer 检查

Sanitizer 在开发构建中插入检测代码，报告测试运行时发现的错误。

| 工具 | 检查范围 |
| --- | --- |
| AddressSanitizer | 内存错误，例如越界访问、释放后继续访问和重复释放 |
| ThreadSanitizer | 未正确同步的并发内存访问，且至少一次访问是写入 |
| UndefinedBehaviorSanitizer | 部分未定义行为，例如有符号整数溢出、无效移位和未对齐访问 |

AddressSanitizer/UndefinedBehaviorSanitizer 与 ThreadSanitizer 使用独立构建。它们会增加运行开销，发布的性能测量关闭这些
工具。测试通过只覆盖本次执行的代码路径和线程调度，不能证明所有错误都不存在。

## CI 与未覆盖范围

CI 在 Linux／macOS／Windows 检查两种依赖方案，Linux 另运行
AddressSanitizer/UndefinedBehaviorSanitizer 和 ThreadSanitizer。
分组名称包含系统和检查模式；步骤名称区分依赖与 TLS 配置。七个原生分组、文档与格式检查
和 `CI - required` 一并保留。
源分支 PR 修改已映射的模块时，在 Linux/Boost/TLS/Release 执行选中的原生检查；
其他配置只记录检查范围。包含代码的合并队列候选和手动触发执行完整的 12 种原生配置。
源分支模块映射之外的改动执行完整 Linux Boost/TLS Release 套件。纯文档候选执行文档和格式检查。
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

Linux Boost 和 Standalone Asio 的 AddressSanitizer（内存错误检测）与 UndefinedBehaviorSanitizer（未定义行为检测）构建
可以试验性选择两个编译槽。构建开始前，Node helper 要求以下条件同时满足：

- `os.availableParallelism()` 报告至少四个可用 CPU 槽。
- 有效内存上限至少为 15 GiB：取实测 `os.totalmem()` 与正的 `process.constrainedMemory()` 限额中的较小值。
- `process.availableMemory()` 独立报告进程仍有至少 12 GiB 可用内存，且不超过上述有效上限。

受限内存返回零时，记录为 `no-reported-constraint`，不再添加数值上限；此时使用实测总内存，
仍须独立通过可用内存与 CPU 检查。零不能证明内存无限。正的约束值必须是有限整数，先与已验证的物理总量取较小值，
再验证有效上限是安全整数。超过物理总量的外部限额记录为 `reported-above-host-memory`，不推断内存无限。
CPU、物理总量和可用内存仍须为正的安全整数；非有限、负数、小数或字符串形式的约束无效。
观测缺失、非法、矛盾或不足时选择一个槽。
除下文的 Windows Boost Release 准入外，其他配置保留现有并行度；源模块目标、sccache launcher、全部十二格矩阵、原生测试和 60 分钟期限保持不变。
required gate 核对分组内的逐配置规划步骤仅在两个目标原生配置中实际成功执行。

helper 在任务日志和摘要中输出真实观测、阈值、所选并行度与原因。这是保守的调度启发式：
12 GiB 可用内存是两个编译槽及其开销的准入预算，不是实测编译峰值或强制单槽上限。
耗时元数据没有编译器／链接器内存峰值，应用基准 RSS 不能补足这项证据。
合入前必须通过完整托管矩阵；本地检查不证明提速。发生内存耗尽时应保留日志并修正，不能自动重试以掩盖失败。

Windows Boost TLS Release 可试验性选择四个编译槽，在构建步骤内部开始编译前，
使用相同的 CPU、有效内存和可用内存准入条件。观测缺失、非法、矛盾或不足时，保留原有三个槽。
Windows Standalone Asio Release 和其他 Release 配置仍使用三个槽。
构建日志与步骤摘要记录实际资源、阈值、所选并行度和原因。该决策不依赖缓存恢复或 compiler launcher 配置。
全部默认构建目标、示例、基准、翻译单元、编译参数和测试命令均保留；构建失败后不会降低并行度自动重试。

核对实际构建日志中的所选并行度，并要求全部十二种配置在不变的任务期限内完成。
归档冷启动与已有缓存分别验收；取消的运行不构成成功覆盖。
本地资源与 CMake 构建图契约检查不能测量编译器内存峰值、提速或托管耗时。
正确性和预算验收均不能以缓存命中为前提。

API 语义参考：[Node 操作系统资源接口](https://nodejs.org/api/os.html#osavailableparallelism)、
[受限内存](https://nodejs.org/api/process.html#processconstrainedmemory)、
[可用内存](https://nodejs.org/api/process.html#processavailablememory)。

### 原生构建缓存

每个实际执行原生检查的分组使用 Mozilla 官方 sccache action `v0.0.11`
和 sccache `v0.16.0`。CMake 通过两个 compiler launcher 选项传入缓存工具；
编译器、构建类型、编译参数、选择的目标和测试命令保持不变。
缓存工具安装或服务启动失败时，直接调用编译器。服务通信失败时，
`SCCACHE_IGNORE_SERVER_IO_ERROR=1` 使编译回退到本地编译器。

安装包消费项目的独立子配置也会接收父构建可选的 `CMAKE_CXX_COMPILER_LAUNCHER`，
包括可执行文件路径中的空格和分号分隔的 launcher 参数。空值不引入缓存工具依赖；
编译器错误仍使构建失败。`consumer_launcher` 回归使用真实子构建验证这些边界，
完整 `install_consumer` 检查独立运行。转发 launcher 不代表发生缓存命中或节省了编译时间。

GitHub Actions 分别持久化本地编译缓存和 vcpkg `files` 二进制归档。
每份编译缓存限制为 512 MiB。缓存键包含操作系统、架构、runner 镜像及版本、
目标与宿主 triplet、固定的 vcpkg 版本、manifest／配置／overlay 内容、请求的 features、默认 features 和分组构建配置。
组内 Boost／TLS 配置共用缓存容器，编译命令仍分别计算哈希。
Release、AddressSanitizer/UndefinedBehaviorSanitizer 与 ThreadSanitizer 使用独立配置。
vcpkg 复用依赖前还会校验各软件包的 ABI。在 `auto` 模式下，依赖归档先使用原有精确主键，
未命中时只允许回退到相同完整身份的 `-cold-` 前缀。
编译缓存保留原有配置专属恢复前缀，并使用每次运行的独立后缀保存新增对象。
缓存不包含构建目录或测试结果。

Actions 缓存的可见范围也取决于工作流 ref。运行可以恢复当前 ref 和默认分支中的
匹配缓存。PR merge ref 写入的缓存只供同一个 PR 使用，其他 PR 和 `main` 无法恢复。
合并队列 ref 是临时 ref，写入的缓存也不会进入 `main`。
因此，日常 PR 和队列运行不会更新默认分支的共享缓存。

使用现有的 **CI** 工作流手动预热共享缓存：选择 **Run workflow**，
将分支设为 **main**，然后启动运行。这个手动 `workflow_dispatch` 执行完整原生矩阵，
并将成功生成的缓存保存在 `main` 下。runner 镜像、依赖或构建配置变化后，需要重新预热；
源码变化后，也可通过再次预热更新编译对象。下次手动预热之前，新的 PR 和队列候选
可能重新编译 main 缓存中尚未包含的对象。
这里不新增 push 或定时触发的矩阵，也不将 PR／队列产物提升为 main 缓存。
详见 [GitHub 缓存范围限制](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache)。

#### 可重复的冷缓存与已有缓存验收

手动 `workflow_dispatch` 可选择 `cache_mode=auto`（默认）或 `cold`。
PR 和合并队列运行始终使用 `auto`。非法模式和非手动运行的冷缓存请求会使校验失败。
只验证范围的分组仍跳过缓存准备和原生执行。两种手动模式均执行七个分组、十二种配置，
保留相同的构建目标、测试、资源准入、调度和 60 分钟期限。

`cold` 为每个运行 ID、尝试次数和分组身份生成新的归档键：
`<identity>-cold-<run-id>-<attempt>-archive`，不查询恢复前缀。
末尾固定后缀可避免另一次尝试的键成为部分匹配。
依赖／编译器 ABI 身份和 `SCCACHE_C_CUSTOM_CACHE_BUSTER` 保持不变。
两类归档均不恢复此前内容，组内翻译单元和后续配置之间仍可复用缓存。
这里验证的是归档冷启动，不是完全未缓存的操作系统、工具链或源码下载环境。
不删除已有缓存。原生任务成功后可保存冷构建产生的归档，供后续 `auto` 使用；
保存和恢复仍受缓存服务可用性及 ref 可见范围限制。

先运行冷缓存矩阵，等待完成后，在同一未变动的 ref 和源码 SHA 上运行 `auto`。
默认分支的共享缓存验收命令如下：

```sh
gh workflow run ci.yml --repo OpenArkStudio/arknet --ref main -f cache_mode=cold
# 冷缓存运行在已记录的 SHA 上成功完成后：
gh workflow run ci.yml --repo OpenArkStudio/arknet --ref main -f cache_mode=auto
```

验收候选时，将 `main` 替换为目标分支或标签，记录两次运行 ID，并核对两个 `headSha`。
遵守现有准入顺序，保留正在执行的运行。旧运行或其他版本的成功不能替代当前版本的两轮验收。

`auto` 保持原有主键兼容：依赖先查询原有精确键，再查询相同完整身份的 `-cold-` 前缀；
编译器保留原有身份前缀，该前缀也覆盖冷构建保存的归档。
前缀恢复成功时，Actions 的 `cache-hit` 仍可能为 `false`；该标志只表示是否精确命中主键。
应核对各恢复 action 日志中的匹配键，以及 vcpkg 恢复／构建数量和 sccache 计数。
仅凭 `false` 不能判定恢复未命中，归档恢复也不能证明其中对象被有效复用。
Auto 可能优先选择较早的精确依赖归档，而不是冷构建新保存的归档。
runner 镜像、编译器或依赖身份变化后，再次未命中属于正常情况。

冷缓存验收要求当前版本实际未恢复旧归档，并成功完成全量原生检查与汇总 gate。
已有缓存验收要求后续全量运行确实恢复归档，并观察到软件包／编译器复用。
没有这些日志，不能将 `auto` 运行称为热缓存运行。缓存失败仍回退到正常编译，
但不能据此认定已有缓存验收通过。本地键规则／CLI 契约不能证明托管结果、原生内存可行性或提速。

缺少缓存标识或归档目录不可用时，关闭持久化。恢复或保存缓存失败不替代编译和测试。
Job 摘要记录归档恢复状态、依赖归档数量和 `sccache --show-stats`；Configure
日志记录 vcpkg 软件包恢复与构建数量。Actions 归档命中本身不能证明编译或依赖命中。
声明性能改善前，需要在 hosted runner 上比较相同源码版本、相同配置的冷、热缓存运行；
本地契约检查不测量 hosted 性能。现有源分支检查范围和完整矩阵的 12 格检查保持不变。

参考资料：[Mozilla action](https://github.com/Mozilla-Actions/sccache-action/tree/v0.0.11)、
[sccache launcher 与回退](https://github.com/mozilla/sccache/tree/v0.16.0)、
[vcpkg 二进制缓存配置](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching)。
