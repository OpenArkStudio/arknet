# UDP

arknet 通过已连接的客户端、按端点创建 session 的服务端，以及未连接的 `udp_cast` socket 提供普通 UDP 数据报。适合由应用协议容忍或检测丢失的发现、遥测和状态更新。数据报保留消息边界，支持空数据报和包含空字节的二进制负载。

## 选择端点

| 类型 | 模型 | 适用业务 |
| --- | --- | --- |
| `udp_client` | 由操作系统关联一个目标端点的 UDP socket | 与一个服务收发数据 |
| `udp_server` / `udp_session` | 一个绑定的 socket；按来源 IP 和端口维护应用 session | 保存每个对端的状态并回复 |
| `udp_cast` | 一个绑定但未连接的 socket；每次接收提供发送方端点 | 发现服务和多目标通信 |
| `udp_client_t<Derived>`、`udp_session_t<Derived>`、`udp_cast_t<Derived>` | 保留原组件的 CRTP 扩展 | 应用状态和收发过滤 |
| `udp_server_t<Session>` | 应用定义的 session 类型 | 扩展每端点状态 |

来源端点首次发送数据报时，服务端创建 session。connect 事件表示本地 session 建立，不代表网络握手。空闲定时器可清理 session 状态；之后的数据报可创建新 session。NAT 映射或来源端口发生变化后，会被视为不同端点。停止一个 session 不会关闭服务端共享的 socket。

客户端保留定时器、任务投递、重连策略和有容量限制的发送。Cast socket 可直接向 Asio endpoint 发送，也可在发送时解析 host/service。广播和组播需要显式设置原生 socket 选项，并配置网络与接口；`udp_cast` 不负责自动发现或管理组成员。

## 交付与大小限制

UDP 不保证交付、顺序、去重、对端可用性或拥塞控制。客户端 `start` 成功只表示目标已关联，不证明服务端正在监听。发送完成表示本地 socket 操作完成，不表示远端接收。客户端自动重连不会重传丢失的数据报。

每次接收预留 65536 字节；配置的接收上限小于 65536 时，启动会被拒绝。这避免因初始缓冲区过小而截断第一个普通数据报。操作系统限制和网络 MTU 仍然生效。IPv4 理论负载上限为 65507 字节，但更小的系统限制可能产生 `message_size`，大包也可能需要 IP 分片。应按实际网络选择负载大小。

接收 view 和 cast 发送方引用只在回调期间有效。被接受的异步发送持有负载和目标直到完成。发送队列同时限制字节数和操作数；入队成功与远端交付是不同结果。

## 调度与延后特性

一个 UDP 服务端的所有 session 共享 socket 和串行 IO 通道。即使多个线程运行同一个外部 context，该服务端的接收回调也不会并行执行。独立客户端、cast socket 或服务端可使用不同通道。应缩短回调执行时间，并同步共享的应用状态。

首版不提供 KCP 或 DTLS。可靠传输、UDP 之上的分片、身份认证、重放防护和加密，需要应用协议或后续传输能力。详见 [IO 模型](../threading_CN.md)。

## 后续阅读

- [用法](usage_CN.md)：回显、cast 目标、socket 选项和关闭顺序。
- [性能](performance_CN.md)：限速流量、丢失校验和 IO 模型对比。
- [UDP 测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/udp.cpp)：空数据报、二进制负载、端点 session、重启、所有权和并发入队。
