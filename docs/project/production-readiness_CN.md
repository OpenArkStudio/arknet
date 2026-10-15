# 生产验收与 ARK 接入

arknet 的目标是用于生产环境，并在 ARK 中直接替换 zephyr。
回显示例只能验证基本功能。正式采用还需要资源上限、故障恢复、并发检查与调用方业务回归。

## 验收条件

| 范围 | 所需证据 |
| --- | --- |
| 协议正确性 | 二进制与空消息、分片、顺序、重复、无效输入及独立 ARQ/FEC 实现互通 |
| 生命周期 | 并发发送与统计、回调内停止、重连、服务端重启，以及完成通知恰好执行一次 |
| 资源限制 | ARQ、FEC、socket 队列与服务端会话数量均有上限；过载时明确返回错误 |
| 执行模型 | 单 context 单线程、单 context 多线程、多 context 各一线程；端点状态经 strand 串行执行 |
| 平台 | Standalone Asio 与 Boost.Asio；Linux、macOS、Windows 构建和测试；独立 ASan/UBSan 与 TSan 检查 |
| 性能 | 本机 Release 构建，多轮测量、包体、在途请求、随机与突发丢包，吞吐量、p50/p99 RTT、CPU 占用和报文开销 |
| 持续运行 | 持续流量下的交付与完成计数、断连及保留资源；短时检查必须写明时长 |
| ARK 兼容 | 真实 zephyr/arknet 混合对端；替换后的 ARK 拆包、事件派发、Session 所有权、重连与关闭 |
| 部署链路 | 目标网络上的跨主机传输、MTU 与 socket 缓冲配置、实际并发量和运行时长 |
| 身份与访问控制 | RUDP 不提供认证与加密；按目标网络验收部署侧访问控制和应用身份验证 |

[测试与验证](testing_CN.md)及 [RUDP 报告](../performance/rudp_CN.md)记录实际执行范围与结果。
本机回环与模拟丢包不能确定部署网络上的生产容量。生产部署仍需补齐下面列出的验收证据。

## 验收状态

| 范围 | 证据 |
| --- | --- |
| 本机完整检查，源码快照 `6c8418e` | Debug + TLS：Boost.Asio 为 52 项检查／470 个用例／978,433 项断言；Standalone Asio 为 51／410／576,824。已启用的默认测试均通过，无失败或环境条件导致的跳过；未启用归档的 Zephyr 互通测试 |
| 内存与并发，同一源码快照 | 完整 Boost.Asio ASan+UBSan 为 52 项检查／470 个用例／615,937 项断言；完整 Standalone Asio TSan 为 51／410／333,108；无 Sanitizer 诊断。关闭泄漏检测，安装包消费项目的子构建未启用检测 |
| 真实数据库 | 四套完整配置均使用隔离的 MySQL Community 8.4.11（TLS）和 Redis 7.4.2（TCP/TLS/mTLS）；覆盖客户端／连接池停止及任务提交失败恢复 |
| 补充停止行为验证 | 指定 Boost 无 TLS 目标：8 项检查／66 个用例／1,515 项断言；ASan 排队连接、HTTP 外部 context 与外部停止：72 次进程检查／280 个用例／4,664 项断言 |
| 纯测试改动 `25bb9f8` | 四种依赖／检测工具组合各通过 30 个专项用例，另通过 128 次指定原生错误用例；MSVC CRT 分配路径在 Windows CI 中执行，本机 macOS 验证不覆盖 |
| 跨平台发布条件 | [PR #16](https://github.com/OpenArkStudio/arknet/pull/16) 发布前须通过全部 14 项必需检查；本机结果不代替候选版本自身的检查 |
| 本机性能 | Release 测量标识产品版本 `2aa5bc0`，见[性能对比报告](../performance/index_CN.md)。Redis 协议性能使用 Valkey 8.0.11，与 Redis 7.4.2 正确性检查分开记录 |
| 真实旧库对端，2026-10-13 | 两种依赖方案各完成 12 组进程场景、16 个会话、512 条消息及回显；独立归档证据 |
| RUDP/FEC 实测负载 | 96 组配置、276 次测量、7,960,439 次成功消息往返；六组 120 秒持续运行 |
| ark 整合 | [#38](https://github.com/OpenArkStudio/ark/pull/38) 完成直接替换与 C++20 升级；[#41](https://github.com/OpenArkStudio/ark/pull/41) 整理核心／官方插件；[#43](https://github.com/OpenArkStudio/ark/pull/43) 统一 snake_case 接口与风格；[#42](https://github.com/OpenArkStudio/ark/pull/42) 调整接收缓冲；均已合并 |
| 生产部署 | 目标网络跨主机容量、生产时长耐久、业务验收及部署访问控制待完成 |

[测试与验证](testing_CN.md)记录配置、源码快照和 Sanitizer 限制。
多个所有者同时完整停止的串行化保证限定于 TCP 服务端及其 HTTP／WebSocket／TLS 派生服务端，
不扩大为所有协议的通用保证。宿主须先停止外部提交方，并在排空期间保持外部 context 的
执行线程运行。本机结果不能证明动态库可安全卸载，也不能证明集群 PCU 容量。

[RUDP/FEC 记录](https://github.com/OpenArkStudio/arknet/tree/main/tests/results/macmini-m4pro-20261013/rudp-fec)
保留独立的互通专项证据。ark 直接替换已合并，业务行为与部署验收仍需使用对应负载测试。

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

- 将 ARK 最低标准提高到 C++20，暂不进行协程或新语言特性改造。
- 直接替换端点和回调类型，移除 zephyr 头文件及构建兼容层。
- 使用 `async_send()` 保持 ARK 的异步发送接纳语义。arknet `send()` 会等待，并返回字节数。
- 同时处理接纳返回值和异步完成错误。RUDP 入队后仍可能因 ARQ 配额不足而失败；将错误转到 ARK 更新线程的事件路径，并定义重试或拒绝策略。
- connect/disconnect/start/stop 回调不带错误参数，在回调入口读取 `get_last_error()`；RUDP handshake 回调仍带 `error_code` 参数。
- 按含义迁移构造参数、静默超时、缓冲与重连设置，两者的位置参数不同。
- 保留 ARK 事件队列与更新线程派发，业务回调不转到 IO 线程执行。
- 首次接入明确设置 ARQ 参数：zephyr 使用 MTU 1400 且关闭拥塞控制；arknet 默认 MTU 1200 且启用拥塞控制。
- FEC 保持可选。支持 FEC 的服务端可接受旧 ARQ 会话；FEC 客户端要求对端支持协商。

ARK 直接替换已合并。生产部署前仍需完成业务回归、跨主机容量和生产时长耐久验证。
