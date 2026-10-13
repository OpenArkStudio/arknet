# RUDP 配置

两端均在停止状态配置。无效参数抛出 `std::invalid_argument`，运行时修改配置
抛出 `std::logic_error`。
可选 FEC 的配置与统计见 [FEC 策略](fec_CN.md)。

```cpp
arknet::arq::options settings;
settings.mtu = 1200;
settings.send_window = 128;
settings.receive_window = 512;
settings.max_pending_bytes = 4 * 1024 * 1024;
server.set_rudp_options(settings);
client.set_rudp_options(settings);
```

## 默认值与限制

| 参数 | 默认值 | 含义 |
| --- | ---: | --- |
| `mtu` | 1200 字节 | 发出的 UDP 载荷上限，包含 ARQ 头部 |
| `send_window` | 128 个 segment | 未确认发送 segment 的窗口上限 |
| `receive_window` | 512 个 segment | 接收排序与完整消息队列的窗口 |
| `interval` | 10 ms | ARQ 定时器周期 |
| `nodelay` | `true` | 使用较低的最小重传超时 |
| `congestion_control` | `true` | 根据丢失与 ACK 调整发送窗口 |
| `fast_resend` | 2 | 触发快速重传所需的后续 ACK 数量；零表示禁用 |
| `max_retransmits` | 20 | 首次发送之后的重试次数 |
| `max_pending_bytes` | 4 MiB | 每个方向的 ARQ 载荷与头部预算，同时限制排队的原始发送数据 |

快速重传最多触发到第六次总发送，之后继续通过超时重传处理丢失，
避免密集或重复 ACK 在对端响应前耗尽重试预算。

服务端默认最多 **1024 个会话**，通过 `set_max_sessions(count)` 在启动前配置。
达到限额时不分配新会话，客户端可重试；现有会话的有效握手重试仍可得到回复。
停止后的对象与仍被应用持有的 Session 不计入活动限额，但其内存由持有者负责释放。

每个 segment 的头部为 24 字节。一条消息最多 256 个分片，默认 MTU 下最大
载荷为 **301,056 字节**。两端接收窗口必须容纳最大消息的全部分片。
降低 MTU 会增加分片数。应按实际网络路径配置 MTU：1200 是保守默认值，
不能保证所有路径都不会发生 IP 分片。

## 发送与过载

`async_send` 复制发送数据，完成通知只执行一次。返回值表示进入 arknet 外层
发送队列；ARQ 队列已满时，后续回调或 future 仍可能报告 `no_buffer_space`。
`send` 返回接纳的载荷字节数，空消息成功时返回零，需要结合错误码判断。

消息过大返回 `message_size`。有效输入超出 ARQ 接收预算时，关闭会话并报告
`no_buffer_space`；无效报文被忽略。重试耗尽报告 `timed_out`。
操作系统 socket 缓冲与 ARQ 队列是不同层次，详见[缓冲配置](../../guides/socket-buffers_CN.md)。

## 执行与停止

内置 IO pool、单 context 多线程、多 context 各一线程使用同一套端点 API。
协议状态与回调通过端点 strand 串行执行。外部 context 必须持续运行到 `stop()`
完成，不要在 IO 回调中阻塞等待 future。

服务端在关闭共享 socket 前尝试发送 FIN。FIN 可能丢失。客户端在未确认数据
耗尽重试时发现静默故障。仅 ARQ 模式不发送空闲心跳；FEC 模式每个统计窗口
发送反馈报文，即使没有应用消息，也会维持对端会话存活。服务端 Session 另有可配置的
静默超时。重连创建新 conversation，不重放旧队列。
不同 sync 的 SYN 不替换活动 Session。复用相同来源端点时，需要先用有效 FIN
关闭旧会话，或者等待其静默超时（默认 60 秒）。
静默时长使用单调时钟；无效数据报、错误 conversation 与过期握手不延长会话寿命。

## 报文兼容范围

ARQ 核心实现兼容 KCP 消息模式的 PUSH/ACK/WASK/WINS segment，使用小端
24 字节头部。与官方 C 实现的测试覆盖双向传输、丢失、重复、乱序和零窗口恢复。
不支持 stream mode。

RUDP 传输另有 5 字节 SYN（`type=1`、`sync32`）和 9 字节 ACK/FIN
（`type=2/3`、`sync32`、`conversation32`），报文布局兼容 ARK 现有 RDP 握手。
核心互通与连接报文分别经过测试，尚未验证 ARK 整体迁移。仅实现裸 KCP 的对端
需要实现该连接报文，或者直接使用 `arknet::arq::control_block`。

核心仅依赖标准库，保持 header-only。调用必须串行，输出回调不能重入核心，
输出 view 在回调返回后失效。`send` 成功返回 0，不支持的长度或失败状态返回 -1，
发送超限返回 -2。`input` 成功返回 0，接收超限返回 -5，其他负值表示无效报文。

连接报文和 ARQ 核心均不认证数据来源。应使用可信网络，或按部署要求设计应用安全机制。
