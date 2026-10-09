# WebSocket

arknet 提供 WebSocket 客户端、服务端和 session，用于持久的双向消息通信。适合浏览器连接、聊天、通知和双方无需轮询即可发送的控制通道。`wss_client`、`wss_server` 和 `wss_session` 通过 [TLS](../tls/overview_CN.md) 加密。

## 协议与对象

| 类型 | 职责 |
| --- | --- |
| `ws_client` | TCP 连接、HTTP 升级、消息收发和按配置重连 |
| `ws_server` / `ws_session` | 接受 TCP 连接、升级协议并处理每个对端 |
| `ws_client_t<Derived>` / `ws_session_t<Derived>` | CRTP 扩展点 |
| `ws_server_t<Session>` | 应用定义的 session 类型 |
| `ws_stream()` | 用于配置和查询协议的原生 Beast stream |

实现基于 Beast：standalone Asio 模式使用适配后的内置头文件，Boost.Asio 模式使用官方 Boost.Beast。两个后端提供相同的 arknet 端点 API。详见 [后端选择](../guide_CN.md)。

客户端升级目标默认为 `/`，可携带路径和 query。通过 `get_upgrade_response()` 和 `get_upgrade_request()` 访问客户端响应与服务端请求。`bind_upgrade` 报告升级完成；成功升级后，connect 回调表示 WebSocket 连接可用。WSS 先完成 TLS 握手，再进行 HTTP 升级。

接收回调提供完整 WebSocket 消息；Beast 负责重组分片消息。默认发送文本帧。二进制模式支持任意字节，文本消息必须是有效 UTF-8。`got_binary()` 表示当前接收消息的类型。协议 stream 处理 ping、pong 和 close 帧，这些帧不会作为应用消息交付。WebSocket 压缩延后纳入支持范围。

## 所有权与应用规则

接收 view 只在回调期间有效。被接受的发送持有负载直到完成。入队成功和写入完成不证明对端应用已经处理消息；需要业务确认时，应定义消息 ID 和应答。

定时器、任务投递、发送队列限制、连接超时、session 空闲限制和客户端重连遵循 TCP 生命周期。重连会重建 stream 并重新升级协议；应在对应生命周期回调中重新设置初始化选项和 decorator。旧消息不会被重放。

这些是专用 WebSocket 端点，不提供 HTTP 路由器、浏览器 Origin 授权、业务登录或自动子协议选择。应用需要自行定义这些规则。普通 HTTP 消息使用独立的 HTTP 端点。

## 调度与关闭

Socket 和协议操作在串行 IO 通道上执行。在客户端 init/connect、服务端 accept 或对象自身通道中配置原生 stream。绕过 arknet 发送队列，执行相互重叠的原生写入或关闭，会破坏操作顺序约定。

内置池和外部 context 遵循 [IO 模型](../threading_CN.md)。外部服务端只有一个执行通道时，session 串行执行；多个通道允许独立 session 并行。关闭时尝试 WebSocket close 握手，并用断开超时限制等待时间，再清理传输。对象和外部运行线程必须保持有效，直到停止完成。

## 后续阅读

- [用法](usage_CN.md)：二进制回显、升级 decorator、容量限制和自定义 session。
- [性能](performance_CN.md)：不同 IO 模型下的二进制消息基准。
- [WebSocket 测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/websocket.cpp)：文本与二进制消息、升级路径和自定义端点。
