# WebSocket 配置与进阶使用

[客户端](client_CN.md)与[服务端](server_CN.md)示例提供完整的启动、收发与关闭程序。以下配置在相应对象启动前应用，除非另有说明。

## 安全 WebSocket（WSS）

开启 `ARKNET_ENABLE_SSL=ON` 后，将服务端头文件和类型替换为 `<arknet/websocket/wss_server.hpp>` 与 `arknet::wss_server`，客户端替换为 `<arknet/websocket/wss_client.hpp>` 与 `arknet::wss_client`。启动前，按 [TLS 服务端与客户端示例](../tls/configuration_CN.md)加载服务端证书及私钥、客户端 CA。使用测试证书时，客户端连接 `localhost`，并保留 `/echo` 目标。服务端启动仍只传监听地址和端口；WSS 两端都不使用 `use_dgram`。

## 配置升级头并查询结果

启动前在生命周期回调中注册 decorator，以便重连重建 stream 时重新应用：

```cpp
client.bind_init([&]
{
    client.ws_stream().set_option(websocket::stream_base::decorator(
        [](websocket::request_type& request)
        {
            request.set(http::field::user_agent, "arknet-example");
        }));
});
server.bind_accept([](auto& session)
{
    session->ws_stream().set_option(websocket::stream_base::decorator(
        [](websocket::response_type& response)
        {
            response.set(http::field::server, "arknet-example");
        }));
});
server.bind_upgrade([](auto& session)
{
    if (!arknet::get_last_error())
        std::cout << session->get_upgrade_request().target() << '\n';
});
```

使用 `client.start(host, port, "/path?key=value")` 指定目标，或用 `set_upgrade_target` 设置默认目标。启动成功后，`get_upgrade_response()` 提供 HTTP 升级响应。升级回调报告完成及错误，不是升级前的 HTTP 路由或认证 API。允许对端执行业务前，应定义 Origin、凭据和子协议规则。

## 容量、自定义 session 与关闭

构造函数中的接收上限同时用于原生 stream 的最大消息大小，默认 16 MiB。在启动前配置消息和发送限制。发送队列默认最多排队 16 MiB 负载和 1024 个操作，使用 `set_max_send_buffer_size` 修改字节上限。入队失败报告 `no_buffer_space`，使用 `get_queued_send_buffer_size()` 查询排队负载字节数。

`async_send` 持有被接受的负载。布尔返回值表示入队是否成功；完成回调表示本地写入，不代表业务交付。回调支持 `(error_code, bytes)`、`(bytes)` 和 `()`；`asio::use_future` 返回包含错误和字节数的 future。立即失败可能在提交线程调用完成回调，被接受的发送在串行通道完成。保存接收 view 前应复制数据，不能在 IO 回调内等待发送 future。

通过 `ws_session_t<MySession>` 扩展 session，并使用 `ws_server_t<MySession>`；通过 `ws_client_t<MyClient>` 扩展客户端。启用 TLS 后，`wss_session_t`、`wss_server_t` 和 `wss_client_t` 提供相同模式。在状态所属通道访问状态；多个外部通道可能并行执行。

自动重连会重建协议 stream，不会重放待处理业务消息。由应用管理重试时，可设置 `set_auto_reconnect(false)`。使用 `set_connect_timeout` 和 `set_disconnect_timeout` 配置连接与断开等待期限。

IO 回调内使用 `request_stop()`，随后由所有者线程调用 `wait_stopped()` 或 `stop()` 等待。关闭时先尝试 close 握手，再清理传输。外部 context 仍运行时停止并销毁端点，随后再停止运行线程。另见 [IO 模型](../../concepts/io-models_CN.md)、[测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/websocket.cpp)及[性能](performance_CN.md)。
