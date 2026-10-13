# RUDP

RUDP 在 UDP 上提供可靠、有序的消息传输。C++20 ARQ 核心负责分片、确认、
重传与接收排序，公开客户端、服务端与 Session 延续 arknet 的 CRTP 和组件组合架构。

<Aside type="note" title="说明：实验性模块">

该模块处于实验阶段，不包含在 `v0.1.0-rc.1` 中。
提供仅 ARQ、固定 FEC 和自适应 FEC 三种[策略](fec_CN.md)。默认仅 ARQ；
FEC 需要两端协商，仍由 ARQ 保证完整消息交付。生产采用须按部署链路和 ARK 业务验收。

</Aside>

## 开始使用

- [创建服务端](server_CN.md)：接收并回显完整消息。
- [创建客户端](client_CN.md)：完成握手并验证回显。
- [配置](configuration_CN.md)：MTU、窗口、限制和兼容范围。
- [FEC 策略](fec_CN.md)：编码参数、动态阈值与统计。
- [性能](performance_CN.md)：本地测量与复现。

| 能力 | 行为 |
| --- | --- |
| 端点 | `rudp_client`、`rudp_server`、`rudp_session` 及 CRTP 变体 |
| 地址与后端 | IPv4/IPv6；Standalone Asio 或 Boost.Asio |
| 消息 | 空消息、二进制消息、分片与有序重组 |
| 可靠性 | ACK、超时重传、快速重传与有限重试 |
| 可选恢复 | Reed–Solomon 固定或自适应 FEC；未恢复数据由 ARQ 重传 |
| 流量控制 | 发送与接收窗口；默认启用拥塞控制 |
| 运行机制 | 握手超时、重连、定时器、内置或外部 IO context |
| 限额 | ARQ、FEC、socket 队列与服务端会话数量均有上限 |

## 选择传输方式

应用自行处理丢失与顺序时，使用[原生 UDP](../udp/index_CN.md)。需要通过 UDP
可靠传递完整消息时，使用 RUDP。重传会增加延迟，有序交付需要等待缺失分片。
可靠字节流通常使用 [TCP](../tcp/index_CN.md)。

RUDP 使用兼容 KCP **消息模式**的 segment，另有 SYN/ACK/FIN 连接报文。
它不能直接与只收发裸 KCP segment 的端点互换，也不提供认证、加密、NAT 穿透、
流模式或自动路径 MTU 探测。详见[兼容范围](configuration_CN.md#报文兼容范围)。

发送完成表示进入可靠队列，不表示对端已经 ACK 或业务已经接收。断线后，未确认
消息不会自动重放到新会话。业务需要收据或重放时，应在应用协议中定义相应行为。
