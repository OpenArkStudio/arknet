# Features

Choose a transport by its message semantics and peer requirements, or a native
service client by its protocol. Transport endpoints share lifecycle, owned-send
and IO scheduling conventions; service clients document their request and reply contracts.

| Module | Server | Client | Configuration | Performance |
| --- | --- | --- | --- | --- |
| [TCP](tcp/index.md) | [Create](tcp/server.md) | [Create](tcp/client.md) | [Framing and sessions](tcp/configuration.md) | [Results](tcp/performance.md) |
| [UDP](udp/index.md) | [Create](udp/server.md) | [Create](udp/client.md) | [Cast and datagram limits](udp/configuration.md) | [Results](udp/performance.md) |
| [RUDP](rudp/index.md) | [Create](rudp/server.md) | [Create](rudp/client.md) | [ARQ and limits](rudp/configuration.md) | [Results](rudp/performance.md) |
| [WebSocket / WSS](websocket/index.md) | [Create](websocket/server.md) | [Create](websocket/client.md) | [Upgrade and messages](websocket/configuration.md) | [Results](websocket/performance.md) |
| [HTTP / HTTPS](http/index.md) | [Create](http/server.md) | [Create](http/client.md) | [Messages and Keep-Alive](http/configuration.md) | [Results](http/performance.md) |
| [TLS / mTLS](tls/index.md) | [Create](tls/server.md) | [Create](tls/client.md) | [Certificates and mutual authentication](tls/configuration.md) | [Results](tls/performance.md) |
| [Redis](redis/index.md) | External Redis service | [Client and pool](redis/usage.md) | [Limits, authentication and leases](redis/configuration.md) | [Benchmark](redis/performance.md) |

## Availability

- Both Standalone Asio and Boost.Asio expose the same callback-based arknet API.
- TLS, HTTPS and WSS require `ARKNET_ENABLE_SSL` and OpenSSL.
- IPv4 and IPv6 share the endpoint API; listening policy is explicit.
- HTTP currently implements HTTP/1.1. Native UDP does not add retransmission or segmentation.
- RUDP includes ARQ-only, fixed FEC and adaptive FEC in `v0.1.0`; local validation is complete. See [deployment acceptance](../project/production-readiness.md).
- [Single HTTP requests](http/requests.md) support completion, timeout and cancellation.
- Public coroutine endpoints, HTTP/2, HTTP/3, named pipes, Unix domain sockets,
  RPC, DTLS and WebSocket compression are [planned](../project/roadmap.md).
- Native MySQL and Redis clients are planned; the [database client guide](database-clients.md)
  defines their single-node scope, failure handling and acceptance criteria.

[Installation](../get-started/installation.md) · [Common concepts](../concepts/index.md) · [API index](../reference/api.md)
