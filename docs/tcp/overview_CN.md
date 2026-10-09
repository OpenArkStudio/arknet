# TCP

arknet 提供 TCP 客户端、服务端和每连接 session，用于建立持久、有序的字节流。适合服务间连接、设备协议，以及由应用自行定义拆包规则的协议。加密 TCP 通过 [TLS](../tls/overview_CN.md) 提供，保留相同的对象模型。

## 对象与扩展

| 类型 | 职责 |
| --- | --- |
| `tcp_client` | 解析地址、连接、收发数据，并按配置自动重连一个对端 |
| `tcp_server` | 监听、创建 session、通知生命周期事件并停止 session |
| `tcp_session` | 保存一个接入连接及其定时器、状态和发送队列 |
| `tcp_client_t<Derived>` / `tcp_session_t<Derived>` | 通过 CRTP 扩展端点，保留传输组件 |
| `tcp_server_t<Session>` | 创建应用定义的 session 类型 |

监听回调覆盖初始化、连接、断开和接收事件；服务端还提供 accept、start 和 stop 事件。组件提供定时器、`post`、用户数据、连接超时、session 空闲超时、客户端自动重连，以及有容量限制的异步发送。

## 消息边界

TCP 保留字节内容及顺序，不保留每次发送的边界。一次接收回调可能包含某次发送的一部分，也可能合并多次发送的数据。

| 传给 `start` 的接收策略 | 应用约定 |
| --- | --- |
| 不传策略 | 返回可读的字节流，由应用还原消息 |
| `'\n'` 或 `"\r\n"` 等分隔符 | 返回分隔符之前及分隔符本身的数据 |
| `asio::transfer_exactly(n)` 等 Asio 完成条件 | 读取满足条件的字节数后返回 |
| Asio 匹配条件 | 由应用判断完整消息 |
| `arknet::use_dgram` | 自动添加和去除 arknet 长度前缀；两端必须使用同一策略 |

`use_dgram` 仍通过可靠的 TCP 传输。负载长度小于 254 时，前缀为一个字节；长度不超过 65535 时，为标记 `254` 加两字节小端长度；更长的消息使用标记 `255` 加八字节小端长度。支持空消息。该策略属于应用协议约定，不能直接用于任意 TCP 服务。接收缓冲区限制同时覆盖消息负载和长度前缀。

## 所有权与调度

接收的 `std::string_view` 只在回调期间有效。被接受的 `async_send` 操作持有负载直到完成，传入 view、span 或 Asio buffer 也遵循这一规则。返回值只表示入队是否成功；写入完成不表示对端应用已经处理消息。

内置 IO 池的每个 context 使用一个工作线程。外部 context 支持单线程和多线程运行，并通过执行通道串行化操作。直接向服务端传入一个外部 context 时，session 共享一个执行通道；需要多个 session 并行执行时，应提供多个通道。详见 [IO 模型](../threading_CN.md)。

请求 ID、应答确认、业务超时、重试规则和身份认证由应用定义。客户端重连不会重放旧发送，也不能保证业务消息只被处理一次。

## 后续阅读

- [用法](usage_CN.md)：回显、拆包、自定义 session 和关闭顺序。
- [性能](performance_CN.md)：复现 IO 模型与工作负载对比。
- [TCP 测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/tcp.cpp)：缓冲区所有权、长度前缀边界和外部 context 关闭。
