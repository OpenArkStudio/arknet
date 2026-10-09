# HTTP and HTTPS

[Usage](http/usage.md) | [Performance](http/performance.md)

arknet's HTTP client, server and session exchange Beast HTTP/1 messages.
HTTPS uses the same message API over TLS when `ARKNET_ENABLE_SSL` is enabled.
Both Asio providers expose the same public types and callback signatures.

## Capabilities

| Area | Available |
| --- | --- |
| Endpoints | `http_client`, `http_server`, `http_session`; secure `https_*` variants |
| HTTP versions | HTTP/1.0 and HTTP/1.1 |
| Connections | Keep-alive, ordered pipelining, reconnect and custom sessions |
| Parsing | Chunked bodies, HEAD response semantics, information responses and `100-continue` |
| Messages | Owned sends with `string_body` / `empty_body`; receives use `string_body` |
| Application layer | [Method/path routing, parameters, wildcards and middleware](http/routing.md) |
| Limits | Header/body limits and bounded send queues |
| TLS | Certificate chain and DNS/IP verification; optional mTLS |

Use HTTP for request/response APIs and service-to-service messages. Use
[WebSocket](websocket/overview.md) when the application needs a long-lived,
bidirectional message channel after an HTTP upgrade.

## Scope and Defaults

Headers default to 8192 bytes and bodies to 16 MiB. The client limit applies
to responses; server settings are copied into new sessions. Messages are
assembled in memory, rather than delivered as a streaming body API.

The server rejects missing/duplicate HTTP/1.1 Host and unsupported Expect.
Beast validates wire framing; applications validate paths, headers and payload
meaning. The router provides path validation, but not authentication,
authorization or filesystem access.

HTTP/2 and HTTP/3 remain [TODO](roadmap.md#http-engine-boundaries).
Beast implements HTTP/1 and WebSocket; it does not add those protocols.
Multipart helpers, file-body streaming and public HTTP awaitables are also
not provided in this release.

## Read Next

- [Usage](http/usage.md): a complete client/server exchange, pipelining, errors and HTTPS.
- [Routing](http/routing.md): matching, middleware, response semantics and concurrency.
- [Performance](http/performance.md): workloads, topology comparison and local charts.
- [TLS](tls/usage.md) and [lifecycle](runtime.md).
