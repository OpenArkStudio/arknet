# Features

Choose a transport by its message semantics and peer requirements. All implemented
modules use the same lifecycle, owned-send and IO scheduling conventions.

| Module | Server | Client | Configuration | Performance |
| --- | --- | --- | --- | --- |
| [TCP](tcp/index.md) | [Create](tcp/server.md) | [Create](tcp/client.md) | [Framing and sessions](tcp/configuration.md) | [Results](tcp/performance.md) |
| [UDP](udp/index.md) | [Create](udp/server.md) | [Create](udp/client.md) | [Cast and datagram limits](udp/configuration.md) | [Results](udp/performance.md) |
| [WebSocket / WSS](websocket/index.md) | [Create](websocket/server.md) | [Create](websocket/client.md) | [Upgrade and messages](websocket/configuration.md) | [Results](websocket/performance.md) |
| [HTTP / HTTPS](http/index.md) | [Create](http/server.md) | [Create](http/client.md) | [Messages and Keep-Alive](http/configuration.md) | [Results](http/performance.md) |
| [TLS / mTLS](tls/index.md) | [Create](tls/server.md) | [Create](tls/client.md) | [Certificates and mutual authentication](tls/configuration.md) | [Results](tls/performance.md) |

## Availability

- Both standalone Asio and Boost.Asio expose the same callback-based arknet API.
- TLS, HTTPS and WSS require `ARKNET_ENABLE_SSL` and OpenSSL.
- IPv4 and IPv6 share the endpoint API; listening policy is explicit.
- HTTP currently implements HTTP/1.1. Native UDP does not add retransmission or segmentation.
- Public coroutine endpoints, HTTP/2, HTTP/3, KCP, named pipes, Unix domain sockets,
  RPC, DTLS and WebSocket compression are [planned](../project/roadmap.md).

[Installation](../get-started/installation.md) · [Common concepts](../concepts/index.md) · [API index](../reference/api.md)
