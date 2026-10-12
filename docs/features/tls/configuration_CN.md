# TLS TCP 配置与进阶使用

[客户端](client_CN.md)与[服务端](server_CN.md)示例提供完整的启动、收发与关闭程序。以下配置在相应对象启动前应用，除非另有说明。

## 要求客户端证书

mTLS 需要在[示例服务端](server_CN.md)启动之前加入：

```cpp
arknet::error_code server_error;
server.load_verify_file(certs + "/ca.pem", server_error);
if (server_error)
    return 1;
server.set_verify_mode(asio::ssl::verify_peer |
    asio::ssl::verify_fail_if_no_peer_cert, server_error);
if (server_error)
    return 1;
```

创建客户端后、启动客户端前，配置客户端证书：

```cpp
client.set_cert_file(certs + "/ca.pem", certs + "/client.pem",
    certs + "/client-key.pem", "");
if (arknet::get_last_error())
    return 1;
```

保留客户端对服务端身份的校验。没有可信客户端证书时，服务端拒绝连接。TLS 1.3 客户端可能短暂报告启动成功后才收到拒绝，因此 mTLS 准入需要处理断开事件和业务就绪状态，不能只依赖 `start` 返回值。应用权限应单独映射到经过校验的证书身份。

## 对 WSS 与 HTTPS 使用相同规则

WSS 使用 `<arknet/http/wss_client.hpp>` 和 `<arknet/http/wss_server.hpp>` 中的 `wss_client`、`wss_server`。按相同方式配置证书和 CA 信任。服务端启动不传 `use_dgram`，客户端使用 `(host, port, "/echo")`。WebSocket 自行提供消息边界，并在 TLS 之后执行 HTTP 升级。帧类型和升级头见 [WebSocket 用法](../websocket/configuration_CN.md)。

HTTPS 使用 `https_client`、`https_server` 和对应头文件。证书与 mTLS 规则保持一致，收发数据则是 HTTP 请求和响应对象，不是字节 view。详见 [HTTP 用法](../http/configuration_CN.md)。HTTPS 不会自动加入 WebSocket 升级路由，也不改变业务授权规则。

所有安全端点族都提供 `bind_handshake` 和 `ssl_stream()`。诊断握手错误时，在客户端启动前注册回调：

```cpp
client.bind_handshake([&]
{
    const auto error = arknet::get_last_error();
    if (error)
        std::cerr << "TLS handshake: " << error.message() << '\n';
});
```

CA 不可信与 DNS/IP 身份不匹配是不同错误。应检查传给 `start` 的 host、证书 subject alternative name、信任配置和有效期。使用数字 IP 连接时，证书需要包含对应 IP 身份；设置 DNS SNI 名称不能替代 IP 校验。

## 关闭与扩展

安全发送保留普通传输的负载所有权、完成回调签名和容量限制。TLS 写入完成不代表远端业务应答。启动前配置 `set_connect_timeout` 和 `set_disconnect_timeout`；关闭时在断开期限内尝试 TLS close-notify。WSS 先关闭 WebSocket 协议，再关闭 TLS 和 TCP。

通过 `tcps_client_t`、`tcps_session_t` 和 `tcps_server_t` 进行 CRTP 扩展；WSS 和 HTTPS 提供对应的 `_t` 类型。可通过继承使用原生 SSL context 选项。启动前配置这些选项，并在端点 IO 通道串行访问 stream。重连会创建新的 TLS stream 并重新校验，不能继续使用旧的原生 stream 引用。

IO 回调内请求停止，由所有者线程等待。外部 context 必须运行到停止完成；先销毁端点，再停止外部运行线程。另见 [IO 模型](../../concepts/io-models_CN.md)、[安全回归测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/tls.cpp)及[性能](performance_CN.md)。
