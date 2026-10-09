# 公共 API 索引

包含 `<arknet/arknet.hpp>` 可使用所有已启用能力，也可以只包含特定的公开头文件。
安全传输头文件需要启用 `ARKNET_ENABLE_SSL`。

## 公开类型

| 类型族 | 客户端 | 服务端／Session | 指南 |
| --- | --- | --- | --- |
| TCP | `tcp_client`、`tcp_client_t<Derived>` | `tcp_server`、`tcp_server_t<Session>`、`tcp_session_t<Derived>` | [TCP](tcp/usage_CN.md) |
| UDP | `udp_client` | `udp_server`、`udp_session`；独立的 `udp_cast` | [UDP](udp/usage_CN.md) |
| WebSocket | `ws_client` | `ws_server`、`ws_session` | [WebSocket](websocket/usage_CN.md) |
| HTTP | `http_client` | `http_server`、`http_session`、`http_router` | [HTTP](http/usage_CN.md)、[路由](http/routing_CN.md) |
| TLS TCP | `tcps_client` | `tcps_server`、`tcps_session` | [TLS](tls/usage_CN.md) |
| HTTPS | `https_client` | `https_server`、`https_session` | [HTTP](http/usage_CN.md) |
| 安全 WebSocket | `wss_client` | `wss_server`、`wss_session` | [TLS](tls/usage_CN.md) |
| 调度 | `timer`、`io_pool`、`io_t` | `iopool` 是 `io_pool` 的别名 | [运行时](runtime_CN.md)、[IO 模型](threading_CN.md) |

协议指南说明 CRTP 变体与公开头文件路径。UDP 的端点 Session 共享服务端
数据报 socket。HTTP 消息类型位于 `arknet::http`，
WebSocket 协议类型位于 `arknet::websocket`。

## 常用操作

| 分类 | API | 约定 |
| --- | --- | --- |
| 启动 | `start(host, port, ...)`、客户端 `async_start(...)` | 检查返回值和 `get_last_error()`；异步启动通过监听回调报告结果。 |
| 监听 | `bind_recv`、`bind_connect`、`bind_disconnect`、`bind_start`、`bind_stop` | 客户端／服务端与协议的签名不同，应在启动前绑定。 |
| 发送 | `async_send(data, callback)`、future 重载、`send` | 入队和写完成是不同结果，见[所有权与线程](runtime_CN.md)。 |
| 停止 | `request_stop`、`wait_stopped`、`stop` | 网络对象保持存活至停止完成；IO 回调请求停止，所有者等待。 |
| 配额 | `set_max_send_buffer_size`、`get_queued_send_buffer_size` | 超限以 `no_buffer_space` 拒绝；默认配额为 16 MiB／1024 个操作。 |
| HTTP 上限 | `set_http_header_limit`、`set_http_body_limit` | 默认头部 8192 字节、body 16 MiB；新配置从下一条消息的解析器生效。 |
| 调度 | `post`、`dispatch`、`start_timer`、`stop_timer` | 使用对象的 IO lane 执行，不能阻塞工作线程。 |
| 错误状态 | `arknet::get_last_error()` | 线程局部状态；在相关调用／回调线程检查，避免被后续操作覆盖。 |

## 扩展与源码参考

自定义客户端／Session 继承对应协议的 `*_t<Derived>`，
服务端模板接受 Session 类型。参见[架构](design_CN.md)和永久
[测试](https://github.com/OpenArkStudio/arknet/tree/main/tests)。
`base/impl` 与 `detail` 下的实现头文件不是稳定的公共 API。
