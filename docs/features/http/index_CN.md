# HTTP 与 HTTPS

arknet 的 HTTP 客户端、服务端和 Session 交换 Beast HTTP/1 消息。
启用 `ARKNET_ENABLE_SSL` 后，HTTPS 通过 TLS 提供相同消息接口。
两种 Asio 后端公开相同类型和回调签名。

## 开始使用

- [创建服务端](server_CN.md)：监听并接收消息。
- [创建客户端](client_CN.md)：连接、发送并验证响应。
- [配置与进阶使用](configuration_CN.md)：协议选项与限制。
- [性能](performance_CN.md)：本地测量与复现。

## 能力

| 分类 | 已提供 |
| --- | --- |
| 端点 | `http_client`、`http_server`、`http_session`，以及安全的 `https_*` 变体 |
| HTTP 版本 | HTTP/1.0 与 HTTP/1.1 |
| 连接 | keep-alive、有序流水线、重连和自定义 Session |
| 解析 | chunked body、HEAD 响应语义、信息响应和 `100-continue` |
| 消息 | 发送持有 `string_body`／`empty_body`；接收使用 `string_body` |
| 应用层 | [方法／路径路由、参数、通配符和中间件](../../guides/http-routing_CN.md) |
| 上限 | 头部／body 解析上限和有界发送队列 |
| TLS | 证书链与 DNS/IP 验证，可选 mTLS |

HTTP 适用于请求／响应 API 和服务间通信。HTTP Upgrade 后需要长期双向消息通道时，
使用 [WebSocket](../websocket/index_CN.md)。

## 范围与默认值

头部默认上限为 8192 字节，body 默认上限为 16 MiB。客户端上限用于响应，
服务端配置会复制给新 Session。接收消息先在内存中组装，没有流式 body 接口。

服务端拒绝缺失／重复的 HTTP/1.1 Host，以及不支持的 Expect。Beast 验证线路拆包；
应用验证路径、头部和业务数据。路由框架验证路径，但不提供身份认证、授权或文件访问。

HTTP/2 和 HTTP/3 仍为 [TODO](../../project/roadmap_CN.md#http-%E5%BC%95%E6%93%8E%E8%BE%B9%E7%95%8C)。
Beast 提供 HTTP/1 和 WebSocket，不包含这两种协议。
本版也没有 multipart 辅助接口、文件 body 流式传输或公开 HTTP 协程接口。

## 继续阅读

- [配置与进阶使用](configuration_CN.md)：协议选项、扩展与关闭。
- [路由](../../guides/http-routing_CN.md)：匹配、中间件、响应语义和并发。
- [性能](performance_CN.md)：负载、线程模型与本地量化图表。
- [TLS](../tls/index_CN.md)和[生命周期](../../concepts/lifecycle_CN.md)。
