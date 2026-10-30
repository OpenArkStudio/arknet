# HTTP and HTTPS

Requires Boost.Asio and Boost.Beast; unavailable with Standalone Asio.

arknet's HTTP client, server and session exchange Beast HTTP/1 messages.
HTTPS uses the same message API over TLS when `ARKNET_ENABLE_SSL` is enabled.

## Get started

<CardGrid>

<LinkCard title="Create a server" href="server.md" description="Listen and receive messages." />
<LinkCard title="Create a client" href="client.md" description="Connect, send and verify replies." />
<LinkCard title="Send a single request" href="requests.md" description="Completion, timeout and cancellation." />
<LinkCard title="Configuration" href="configuration.md" description="Protocol options, limits, extension and shutdown." />
<LinkCard title="Performance" href="performance.md" description="Workloads, topology comparisons and local charts." />

</CardGrid>

## Capabilities

| Area | Available |
| --- | --- |
| Endpoints | `http_client`, `http_server`, `http_session`; secure `https_*` variants |
| HTTP versions | HTTP/1.0 and HTTP/1.1 |
| Connections | Keep-alive, ordered pipelining, reconnect and custom sessions |
| Parsing | Chunked bodies, HEAD response semantics, information responses and `100-continue` |
| Messages | Owned sends with `string_body` / `empty_body`; receives use `string_body` |
| Application layer | [Method/path routing, parameters, wildcards and middleware](../../guides/http-routing.md) |
| Limits | Header/body limits and bounded send queues |
| TLS | Certificate chain and DNS/IP verification; optional mTLS |

Use HTTP for request/response APIs and service-to-service messages. Use
[WebSocket](../websocket/index.md) when the application needs a long-lived,
bidirectional message channel after an HTTP upgrade.

## Scope and Defaults

Headers default to 8192 bytes and bodies to 16 MiB. The client limit applies
to responses; server settings are copied into new sessions. Messages are
assembled in memory, rather than delivered as a streaming body API.

The server rejects missing/duplicate HTTP/1.1 Host and unsupported Expect.
Beast validates wire framing; applications validate paths, headers and payload
meaning. The router provides path validation, but not authentication,
authorization or filesystem access.

HTTP/2 and HTTP/3 remain [TODO](../../project/roadmap.md#http-engine-boundaries).
Beast implements HTTP/1 and WebSocket; it does not add those protocols.
Multipart helpers, file-body streaming and public HTTP awaitables are also
not provided in this release.

## Read Next

- [Routing](../../guides/http-routing.md): matching, middleware, response semantics and concurrency.
- [TLS](../tls/index.md) and [lifecycle](../../concepts/lifecycle.md).
