# 功能

根据消息语义和对端要求选择传输方式，根据服务协议选择原生客户端。
传输端点采用一致的生命周期、发送所有权与 IO 调度约定；
服务客户端另行说明请求与回复契约。

| 模块 | 服务端 | 客户端 | 配置 | 性能 |
| --- | --- | --- | --- | --- |
| [TCP](tcp/index_CN.md) | [创建](tcp/server_CN.md) | [创建](tcp/client_CN.md) | [拆包与 Session](tcp/configuration_CN.md) | [结果](tcp/performance_CN.md) |
| [UDP](udp/index_CN.md) | [创建](udp/server_CN.md) | [创建](udp/client_CN.md) | [Cast 与数据报边界](udp/configuration_CN.md) | [结果](udp/performance_CN.md) |
| [RUDP](rudp/index_CN.md) | [创建](rudp/server_CN.md) | [创建](rudp/client_CN.md) | [ARQ 与限额](rudp/configuration_CN.md) | [结果](rudp/performance_CN.md) |
| [WebSocket / WSS](websocket/index_CN.md) | [创建](websocket/server_CN.md) | [创建](websocket/client_CN.md) | [Upgrade 与消息](websocket/configuration_CN.md) | [结果](websocket/performance_CN.md) |
| [HTTP / HTTPS](http/index_CN.md) | [创建](http/server_CN.md) | [创建](http/client_CN.md) | [消息与 Keep-Alive](http/configuration_CN.md) | [结果](http/performance_CN.md) |
| [TLS / mTLS](tls/index_CN.md) | [创建](tls/server_CN.md) | [创建](tls/client_CN.md) | [证书与双向认证](tls/configuration_CN.md) | [结果](tls/performance_CN.md) |
| [Redis](redis/index_CN.md) | 外部 Redis 服务 | [客户端与连接池](redis/usage_CN.md) | [限额、认证与租约](redis/configuration_CN.md) | [性能测试](redis/performance_CN.md) |

## 支持状态

- Standalone Asio 与 Boost.Asio 提供相同的 arknet 回调 API。
- TLS、HTTPS 和 WSS 需要启用 `ARKNET_ENABLE_SSL` 并提供 OpenSSL。
- IPv4 和 IPv6 共用端点 API，监听策略需要显式配置。
- HTTP 当前实现 HTTP/1.1。原生 UDP 不增加重传或自动切包。
- RUDP 提供仅 ARQ、固定 FEC 与自适应 FEC，包含在 `v0.1.0` 中；部署采用需完成[生产验收](../project/production-readiness_CN.md)。
- [单次 HTTP 请求](http/requests_CN.md)支持完成通知、超时与取消。
- 公共协程端点、HTTP/2、HTTP/3、named pipe、Unix domain socket、RPC、
  DTLS 和 WebSocket 压缩列在[路线图](../project/roadmap_CN.md)中。

[安装](../get-started/installation_CN.md) · [公共概念](../concepts/index_CN.md) · [API 索引](../reference/api_CN.md)
