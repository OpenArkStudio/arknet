# 生产验收与 ARK 接入

arknet 的目标是用于生产环境，并在 ARK 中直接替换 zephyr。
回显示例只能验证基本功能。正式采用还需要资源上限、故障恢复、并发检查与调用方业务回归。

本文说明当前 main 的能力与 ark 接入边界。[版本说明](releases_CN.md)单独记录标签内的变更；
后续 main 的测试、构建修复和框架接口不归入 v0.2.2。

## 验收条件

| 范围 | 所需证据 |
| --- | --- |
| 协议正确性 | 二进制与空消息、分片、顺序、重复、无效输入及独立 ARQ/FEC 实现互通 |
| 生命周期 | 并发发送与统计、回调内停止、重连、服务端重启，以及完成通知恰好执行一次 |
| 资源限制 | ARQ、FEC、socket 队列与服务端会话数量均有上限；过载时明确返回错误 |
| 执行模型 | 单 context 单线程、单 context 多线程、多 context 各一线程；端点状态经 strand 串行执行 |
| 平台 | Standalone Asio 与 Boost.Asio；Linux、macOS、Windows 构建和测试；独立 AddressSanitizer/UndefinedBehaviorSanitizer 与 ThreadSanitizer 检查 |
| 性能 | 本机 Release 构建，多轮测量、包体、在途请求、随机与突发丢包，吞吐量、p50/p99 RTT、CPU 占用和报文开销 |
| 持续运行 | 持续流量下的交付与完成计数、断连及保留资源；短时检查必须写明时长 |
| ARK 兼容 | 真实 zephyr/arknet 混合对端；替换后的 ARK 拆包、事件派发、Session 所有权、重连与关闭 |
| 部署链路 | 目标网络上的跨主机传输、MTU 与 socket 缓冲配置、实际并发量和运行时长 |
| 身份与访问控制 | RUDP 不提供认证与加密；按目标网络验收部署侧访问控制和应用身份验证 |

[测试与验证](testing_CN.md)、[RUDP/FEC 报告](../performance/rudp_CN.md)和独立的
[RUDP 并发报告](../performance/rudp-concurrency_CN.md)分别记录实际范围与结果。
本机回环与模拟丢包不能确定部署网络上的生产容量。生产部署仍需补齐下面列出的验收证据。

## 验收状态

| 范围 | 证据 |
| --- | --- |
| 本机完整检查，源码快照 `6c8418e` | Debug + TLS：Boost.Asio 为 52 项检查／470 个用例／978,433 项断言；Standalone Asio 为 51／410／576,824。已启用的默认测试均通过，无失败或环境条件导致的跳过；未启用归档的 Zephyr 互通测试 |
| 内存与并发，同一源码快照 | 完整 Boost.Asio AddressSanitizer+UndefinedBehaviorSanitizer 为 52 项检查／470 个用例／615,937 项断言；完整 Standalone Asio ThreadSanitizer 为 51／410／333,108；无 Sanitizer 诊断。关闭泄漏检测，安装包消费项目的子构建未启用检测 |
| 真实数据库 | 四套完整配置均使用隔离的 MySQL Community 8.4.11（TLS）和 Redis 7.4.2（TCP/TLS/mTLS）；覆盖客户端／连接池停止及任务提交失败恢复 |
| 补充停止行为验证 | 指定 Boost 无 TLS 目标：8 项检查／66 个用例／1,515 项断言；AddressSanitizer 排队连接、HTTP 外部 context 与外部停止：72 次进程检查／280 个用例／4,664 项断言 |
| 纯测试改动 `25bb9f8` | 四种依赖／检测工具组合各通过 30 个专项用例，另通过 128 次指定原生错误用例；MSVC CRT 分配路径在 Windows CI 中执行，本机 macOS 验证不覆盖 |
| 连接池关闭快照 `550fb58` | 两种依赖均通过 AddressSanitizer+UndefinedBehaviorSanitizer 与 ThreadSanitizer 下的 MySQL／Redis 关闭测试，覆盖排队 acquire 取消和回调重入；未改动的归还失败目标也通过四套配置。准确计数与源码边界见[测试与验证](testing_CN.md) |
| 跨平台发布条件 | 已发布的 v0.2.2，源码 `c5432ca`，已在[实际合并候选](https://github.com/OpenArkStudio/arknet/actions/runs/39034278627)通过全部 14 项必需检查；本机结果不代替发行候选自身的验证 |
| 本机性能 | Release 测量标识产品版本 `2aa5bc0`，见[性能对比报告](../performance/index_CN.md)。Redis 协议性能使用 Valkey 8.0.11，与 Redis 7.4.2 正确性检查分开记录 |
| 真实旧库对端，2026-10-13 | 两种依赖方案各完成 12 组进程场景、16 个会话、512 条消息及回显；独立归档证据 |
| RUDP/FEC 实测负载 | 96 组配置、276 次测量、7,960,439 次成功消息往返；六组 120 秒持续运行 |
| ark 整合 | [#38](https://github.com/OpenArkStudio/ark/pull/38) 完成直接替换与 C++20 升级；[#41](https://github.com/OpenArkStudio/ark/pull/41) 整理核心／官方插件；[#43](https://github.com/OpenArkStudio/ark/pull/43) 统一 snake_case 接口与风格；[#42](https://github.com/OpenArkStudio/ark/pull/42) 调整接收缓冲；均已合并 |
| ark 公开数据库插件 | 已接受的 ark `cbb65971` 提供可选 [MySQL](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/mysql_CN.md) 与 [Redis](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/redis_CN.md) 协程模块；拥有型配置、管理器共享执行器、自然任务排空与原生资源回收均有明确接口契约。框架能力与 arknet 原生回调 API 分开 |
| 生产部署 | 目标网络跨主机容量、生产时长耐久、业务验收及部署访问控制待完成 |

已发布的 v0.2.2 源码在 macOS 27.0.1 的本机 5,000 连接 TCP 回显负载中出现过响应超时：
每进程 1 个 IO 工作线程、1 KiB 包体、每连接 4 条在途请求，使用 Boost 1.90 或
Standalone Asio 1.38.2 均出现过。原因尚未确认，该负载尚未通过容量验收，详见[版本说明](releases_CN.md)。
main 上的 [PR #26](https://github.com/OpenArkStudio/arknet/pull/26) 改善了基准程序客户端的清理调度，
不属于 v0.2.2，也没有解决这项 TCP 响应超时。

独立对照直接使用官方 Asio，不经过 arknet 端点或运行时代码。保留的本机诊断使用
[描述符限制记录](https://github.com/OpenArkStudio/arknet/pull/54)与
[底层 socket 追踪](https://github.com/OpenArkStudio/arknet/pull/56)的组合，
不属于已发布版本。

| 配置／观测 | 结果 |
| --- | --- |
| 运行标识 | 2026-10-23；组合源码 `cd7205b34152a26cca061b947cd33aaa785248d8`；程序 SHA-256 `dabd26dd9a2c8dd197fdaac23939ace8e94d5a7b59ea170c9e9aeb32d2d5ae04` |
| 主机／构建 | Mac16,11，14 核 CPU，48 GiB，macOS 27.0.1；AppleClang 21 Release，Boost 1.90，关闭 TLS |
| 负载 | 5,000 条回环连接；1 KiB 包体；每连接最多四条在途请求；每进程一个 IO context、一个 IO 工作线程；流量保持 3 秒；请求期限 5 秒；服务端期限 40 秒；启用组合操作与底层 socket 追踪 |
| 完成／清理 | 两端均有 5,000 条连接就绪并开始，4,999 条完成。连接 759 保留请求 65–68；客户端达到请求期限后失败，服务端随后收到对端重置。两端均完成清理 |
| 实际进程限制 | 两端启动记录均为 `RLIMIT_NOFILE` 软限制 1,048,575／硬限制 `"unlimited"`。采样 FD 峰值：客户端 5,008，服务端 5,009；建连错误为零 |
| 主机描述符设置 | 只读快照：`kern.maxfilesperproc` 为 184,320，`kern.maxfiles` 为 368,640。这些主机设置与进程报告的资源限制不同，不能据此确定连接容量 |
| 停滞客户端连接 | 底层与组合读取完成字节均为 69,954，写入完成字节均为 74,318。容量为 4,096 字节的底层读取未完成；无活动写入、应用待发送队列或内核可读字节 |
| 对应服务端连接 | 底层与组合的读取、写入完成字节均为 74,318，响应 68 已完成写入。后续读取在客户端超时后以对端重置结束 |
| TCP 观测 | 客户端发送缓冲字节为零，服务端为 4,364 字节，其中可能包含在途数据；两端发送窗口均为正值。写入完成不等于对端已收到 |
| 头部抓包 | 保留三个文件，记录 1,967,917 个报文；tcpdump 报告内核丢弃数为零。未保留接口丢弃统计和抓包进程退出码，不能仅凭报文缺失认定传输丢失 |
| 抓包中的 TCP 进度 | 两个方向的报文头声明数据范围均覆盖 `[0,74318)`；客户端→服务端最大累计 ACK 为 74,318，服务端→客户端为 69,954。客户端通过 SACK 报告后续范围 `[71045,72136)`、`[73227,74318)`，但累计确认位置没有推进 |
| 记录与线程进展 | 5,000 个会话均有记录，两端均输出未完成会话，无遗漏、输出截断或无效状态转换。两端均完成 13/13 个线程探针；这不能证明每个 socket 的就绪事件正常 |

未完成响应的字节差是四个 1,091 字节的帧，位于服务端写入完成与客户端读取完成之间。
底层与组合操作的生命周期字节总数一致。一次 2,182 字节底层读取后，通过零字节读取完成
回调取出已有缓冲中的后一个帧是合法行为；不能把有界记录的尾部当作完整历史。
目前没有证据证明存在拆包、回调关联或应用队列缺陷。

首个未累计确认的范围 `[69954,71045)` 出现在三个服务端数据报文头中。客户端累计 ACK
一直停在 69,954，直至最后一个服务端数据报文头之后约 4.96 秒发出重置；此处时间只来自抓包。
捕获到报文头不等于接收 TCP 栈已接受这些字节，包体与校验和也未经验证。服务端重置后未能
取得对端地址，关联使用唯一会话标识、客户端实际端点及匹配的服务端本地地址；这一派生关联
属于覆盖范围限制。

根因仍未确定。冻结操作计数器与抓包时钟之间没有经过验证的绝对时间映射；报文头部也不能
证明 socket 就绪或回调已经派发。macOS 的 netstat 手册说明，非 root 读取 TCP 统计时
返回零值，因此保留的非特权计数不能用于证明没有重传或资源丢弃。这是失败诊断，不构成性能结果或容量验收。原始日志、抓包仅留本地，参数见
[连接基准测试](../../benchmarks/CONNECTIONS_CN.md)。

原生对照还提供默认关闭的 `--tcp-kernel-samples on`，要求同时启用 `--operation-trace on`，
用于在失败清理前保留公开 Apple `TCP_CONNECTION_INFO` 周期查询。现有一秒工作线程标记
检查已收到 DATA、尚未完成且至少一秒没有组合读写完成的 traffic 会话。额外固定存储
小于 32 KiB，最多保留 16 个身份、每个身份 8 个样本；已有进展、完成或关闭的会话释放
槽位，持续停滞的身份保留槽位。遗漏和覆盖有明确计数，其他平台返回 null 并说明不支持。
采样时间区别于操作历史冻结时间，延迟采样可能错过瞬时状态。Boost 与 Standalone Asio
相关回归均通过，包括收到 DATA 后主动停止响应的受控测试：超时清理前保留了 ESTABLISHED
状态样本。参见[用法与限制](../../benchmarks/CONNECTIONS_CN.md#可选-tcp-内核采样)。

### 帧边界诊断

原生对照的取消前 `connection_diagnostic` 包含有界 `frame_boundary` 对象。
`status` 区分 `empty`、`incomplete_prefix`、`invalid_prefix`、`incomplete_frame`
和 `complete_frame`。`input_frame_ready` 仅在长度前缀规范、未超限且帧完整时为 true，
非法前缀不再显示为就绪。这只验证帧边界，不表示应用头或包体已经校验通过。

`prefix` 最多包含 9 个无符号字节值，不输出包体字节。`buffered_bytes` 统计当前接收
缓冲中的全部字节，包括后续帧。完整前缀可用前，`declared_body_bytes` 为 null；
解码完成后，即使长度非法或超限，也保留该声明值。`required_wire_bytes` 包含首帧
前缀，`missing_frame_bytes` 表示其尚缺字节数；已缓冲后续字节时，缺额下限为零。
空输入、前缀不完整或前缀非法时，这两个字段为 null，不对未验证长度执行加法。

matcher 和原有 4,096 字节帧上限保持不变。规范的 9 字节前缀至少声明 65,536 字节
包体，因此超过该上限并报告 `invalid_prefix`；不完整的 9 字节前缀仍报告
`incomplete_prefix`。原生 `--self-test` 使用固定字节串验证这些状态、帧边界限制、
64 位极值长度和 JSON 输出，不运行连接负载。此诊断改善没有解释或修复 5,000 连接
停滞，不改变协议、IO 操作、期限或负载，也不增加容量或性能验收证据。

### 停滞连接的内核采样

固定五次诊断使用上表同一主机和负载，启用采样；Boost 1.90 Release，关闭 TLS。
原生源码为 `605625dfea0efff90468771cc0839d03866a134c`，系列运行版本 `9200bfb`
仅增加文档。每次均有 5,000 条连接就绪并开始；连续五次结果不能用于估算故障概率。

| 运行 | 每端完成连接数 | 每端未完成连接数 |
| --- | ---: | ---: |
| 1 | 4,992 | 8 |
| 2 | 4,996 | 4 |
| 3 | 5,000 | 0 |
| 4 | 4,999 | 1 |
| 5 | 4,995 | 5 |

十个进程均完成清理，建连错误为零。18 个未完成连接的身份在两端匹配，36 份历史保留
211 个内核样本。所有保留的客户端样本均报告：

| 公开 TCP 观测 | 数值 |
| --- | --- |
| 连接状态／恢复标志 | ESTABLISHED／正在丢包恢复 |
| 拥塞窗口 | 0 字节 |
| 发送缓冲占用，含在途数据 | 4,364 字节 |
| 对端接收窗口 | 293,184–521,024 字节 |
| 导出的重传超时值 | 0 毫秒 |

每个匹配连接的客户端写入完成字节均比服务端读取完成字节多四个 1,091 字节的帧。
IO 工作线程探针继续完成；最终未完成身份和输出均未遗漏。部分瞬时满足采样条件的观测
未能保留，较早的服务端样本也被覆盖；服务端保留的尾部位于客户端清理之后，两端采样
窗口并不同时。客户端历史从完成进度停止至少一秒后开始，没有记录进入恢复状态的过程。
成功连接没有输出可对比历史，这组运行也不能排除诊断工具对时序的影响。

本机 SDK 定义了这些标志和单位，但导出的零超时值不能确定当前内核实际启用的定时器或
期限。这些观测将调查重点缩小到 TCP 恢复状态，尚不能证明 Asio 或内核缺陷、解释此前抓到
的失败，也不构成容量验收。原始样本仅留本地。

`resource_startup` 描述符诊断通过 `getrlimit(RLIMIT_NOFILE)` 读取运行进程自身的 `nofile_soft` 和
`nofile_hard`。有限值为整数，无限值为 `"unlimited"`，不可用时为 null 并说明原因；
Windows 保留句柄计数，并将该 POSIX 限制标记为不适用。描述符还包括文件、管道和事件循环
资源，不等于连接容量。当前 shell、启动服务或 macOS `kern.maxfiles*` 设置不能确定历史
进程限制，也不是跨平台容量默认值。早期未记录启动限制的证据缺口仍然存在。工具不修改
限制、负载或期限；跨主机与生产容量仍待验收。

[测试与验证](testing_CN.md)记录配置、源码快照和 Sanitizer 限制。
多个所有者同时完整停止的串行化保证限定于 TCP 服务端及其 HTTP／WebSocket／TLS 派生服务端，
不扩大为所有协议的通用保证。宿主须先停止外部提交方，并在排空期间保持外部 context 的
执行线程运行。本机结果不能证明动态库可安全卸载，也不能证明集群 PCU 容量。

[RUDP/FEC 记录](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec)
保留独立的互通专项证据。ark 直接替换已合并，业务行为与部署验收仍需使用对应负载测试。

## 框架协程与数据库边界

arknet 提供原生回调客户端、连接池与 `manual_runtime` 的标准 Asio executor；
它本身没有公开的数据库 `co_await` 外观。ark 的 `mysql_module` 与 `redis_module`
属于独立框架，使用公开的拥有型 `configure()` options 和管理器所拥有的任务组。
原生 IO 仍由 arknet 运行；模块 update 派发完成通知，管理器集中轮询共享业务执行器，
在所属线程恢复业务协程。用法见 [MySQL](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/mysql_CN.md)、
[Redis](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/redis_CN.md) 和 [任务生命周期](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/guides/tasks_CN.md)。

管理器自然停止时先关闭顶层任务准入，让已受理流程继续提交后续步骤，随后在宿主生命周期帧中
回收原生资源。任务计数归零不代表回调、线程或动态库已安全卸载。
公开模块协议对端回归、插件 DSO 加载检查，以及显式选择的真实服务程序验证范围不同；
`check-database` 编译真实服务程序，不连接真实数据库。
[ark 数据库测试指南](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/tests/database/README_CN.md)说明运行前提和边界。
编译和 fixture 检查不能代替显式真实服务运行，也不代表任意 DSO 卸载、热更新或生产容量验收。

## 部署容量目标

ARK 采用多进程部署，目标为集群同时在线 **10 万至 20 万用户（PCU）**。
PCU 是在线用户数，TCP 连接、WebSocket 连接和 RUDP 会话需要分别统计；
一个用户可能持有多个连接，服务间连接也会占用资源。

下面仅按均匀分配计算，用于估算每个入口进程的用户数，尚未确定部署规模，也不是实测容量。

| 入口进程数 | 10 万 PCU：每进程用户数 | 20 万 PCU：每进程用户数 |
| --- | ---: | ---: |
| 16 | 6,250 | 12,500 |
| 32 | 3,125 | 6,250 |
| 64 | 约 1,563 | 3,125 |

容量验收需要补齐以下负载参数，再在目标机器和跨主机网络上执行测试：

- 各协议的连接占比、每用户连接数、每连接消息频率与包体分布。
- 每进程 CPU、内存、线程数、连接上限，以及操作系统文件描述符与 socket 缓冲配置。
- 登录突发、重连突发及入口进程故障后，剩余进程需要承接的连接与消息量。
- 持续运行时长、p99 RTT、吞吐量、CPU 占用、内存增长、错误率与资源拒绝的验收阈值。

RUDP 服务端默认 `set_max_sessions()` 上限为 **1024 个会话**。部署时按实测的单进程容量显式设置，
同时配置 ARQ/FEC 和发送队列上限；提高会话上限本身不构成容量验证。
当前本机性能报告记录消息传输与故障恢复结果，尚未验证集群 10 万至 20 万 PCU。

## ARK 能力对应

| 当前用途 | arknet 替换能力 | 需要保持的行为 |
| --- | --- | --- |
| TCP 客户端／服务端／Session | `tcp_client`、`tcp_server`、`tcp_session` | 原始字节流；保留 ARK 自己的 AFCS/AFSS 拆包 |
| WebSocket 客户端／服务端／Session | `ws_client`、`ws_server`、`ws_session` | 二进制消息与升级回调 |
| UDP 加 `use_kcp` 的 RDP | `rudp_client`、`rudp_server`、`rudp_session` | 与已有 zephyr 对端互通时使用旧 ARQ 报文模式 |
| HTTP 客户端 | `http_request` | 由 `http_client_module_impl::update()` 派发完成通知，保持现有状态码处理 |
| 可选 HTTP／Consul 模块 | HTTP 请求、服务端与路由 | 启用时更新方法与路由 API，并检查已注册路由 |

ARK 的业务消息体上限为 4096 字节，并自行处理业务消息头。传输分片位于这一层之下。
ARK 的业务 Session ID 存入 `user_data`，与 RUDP conversation ID 区分。

## 替换规则

- ARK 使用 C++20；传输回调的直接替换与框架数据库协程接口分别遵循各自的生命周期契约。
- 直接替换端点和回调类型，移除 zephyr 头文件及构建兼容层。
- 使用 `async_send()` 保持 ARK 的异步发送接纳语义。arknet `send()` 会等待，并返回字节数。
- 同时处理接纳返回值和异步完成错误。RUDP 入队后仍可能因 ARQ 配额不足而失败；将错误转到 ARK 更新线程的事件路径，并定义重试或拒绝策略。
- connect/disconnect/start/stop 回调不带错误参数，在回调入口读取 `get_last_error()`；RUDP handshake 回调仍带 `error_code` 参数。
- 按含义迁移构造参数、静默超时、缓冲与重连设置，两者的位置参数不同。
- 保留 ARK 事件队列与更新线程派发，业务回调不转到 IO 线程执行。
- 首次接入明确设置 ARQ 参数：zephyr 使用 MTU 1400 且关闭拥塞控制；arknet 默认 MTU 1200 且启用拥塞控制。
- FEC 保持可选。支持 FEC 的服务端可接受旧 ARQ 会话；FEC 客户端要求对端支持协商。

ARK 直接替换已合并。生产部署前仍需完成业务回归、跨主机容量和生产时长耐久验证。
