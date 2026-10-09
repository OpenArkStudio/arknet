# TLS

TLS 为 TCP、WebSocket 和 HTTP 提供传输加密及基于证书的对端校验。启用 `ARKNET_ENABLE_SSL=ON`，并使用导出的 `arknet::arknet` 接口目标引入可选的 OpenSSL 依赖。最低支持协议版本为 TLS 1.2。

## 安全端点

| 明文类型 | 安全类型 | 应用协议 |
| --- | --- | --- |
| `tcp_client`、`tcp_server`、`tcp_session` | `tcps_client`、`tcps_server`、`tcps_session` | 字节流和 TCP 接收策略 |
| `ws_client`、`ws_server`、`ws_session` | `wss_client`、`wss_server`、`wss_session` | TLS 与 HTTP 升级后的 WebSocket 消息 |
| `http_client`、`http_server`、`http_session` | `https_client`、`https_server`、`https_session` | TLS 上的 HTTP 消息 |

安全端点保留 CRTP 扩展和组件组合模型，包括自定义 session、发送所有权、队列限制、定时器及外部 IO。`ssl_stream()` 等 API 沿用 Asio 的 SSL 类型命名，实际配置的是现代 TLS 协议。首版不提供 UDP DTLS。

## 默认校验规则

客户端校验证书链，以及传给 `start` 的 DNS 名称或 IP 地址。默认加载系统信任路径；通过 `load_verify_file` 添加私有 CA。连接 DNS 名称时发送 SNI，使用数字 IP 时不发送 SNI。证书链可信但 DNS/IP 身份不匹配时，连接仍失败。

服务端启动前必须提供证书链和匹配的私钥。`set_cert_file` 加载 PEM 文件，`set_cert_buffer` 加载 PEM 内容。`bind_handshake` 在连接可用事件之前报告 TLS 握手完成；WSS 随后执行 HTTP 升级。TLS 错误通过常规错误与断开生命周期报告。

显式设置 `verify_none` 会关闭客户端校验，仅适用于明确的本地实验。这会移除默认配置提供的对端身份保护。仓库中的证书和私钥是公开测试文件，不能用于部署。

## 双向 TLS

mTLS 进一步要求客户端证明其持有可信证书。为服务端配置可信客户端 CA，并设置 `asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert`；同时为客户端配置证书和私钥。两端仍需保留正常的信任规则。

该方式适合受控的服务连接和统一管理的设备。证书信任确认密码学意义上的对端身份；应用授权仍需将身份映射到允许的操作。TLS 1.3 客户端可能先完成自身握手，再收到服务端缺少客户端证书的 alert，因此仍需处理连接和断开事件。

## 生命周期与配置

在启动端点之前配置信任、证书和校验规则。除便捷加载函数外，仍可使用原生 Asio SSL context API 设置选项。后续修改配置时，应协调端点生命周期；当前不提供自动证书轮换服务。

`ssl_stream()` 提供原生 TLS stream。Stream 操作遵循端点串行 IO 通道，不能将原生写入与 arknet 发送队列重叠执行。关闭时使用断开超时限制等待 TLS close-notify 的时间。外部 context 必须运行到停止完成。

## 后续阅读

- [用法](usage_CN.md)：校验服务端证书的 TCP 回显、mTLS、WSS 和 HTTPS 配置。
- [性能](performance_CN.md)：TCPS、WSS 和 HTTPS 的稳态测量限制。
- [TLS 测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/tls.cpp)：信任、DNS/IP 拒绝、SNI、mTLS 和重连。
- [HTTP](../http_CN.md)与 [WebSocket](../websocket/overview_CN.md)：应用消息 API。
