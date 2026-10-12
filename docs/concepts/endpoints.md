# Clients, Servers and Sessions

A client manages one peer. A server owns a listener and its sessions. A session
contains a peer's state, callbacks, timers and send queue.

| Object | Responsibility | Lifetime |
| --- | --- | --- |
| Client | Resolve, connect, send, receive and optionally reconnect | Owned by the application; kept alive until stopped |
| Server | Listen, create sessions and coordinate shutdown | Owned by the application; stopped before its IO resources |
| TCP-derived session | One accepted TCP, HTTP, WebSocket or TLS connection | Managed by the server; outstanding operations can retain it |
| UDP session | State associated with a source IP and port | Shares the server socket and serialized receive lane |
| UDP cast | Send and receive native datagrams with explicit endpoints | Owned by the application; no remote handshake |

A UDP session does not authenticate its peer and does not turn datagrams into a
reliable connection. TCP delivery likewise does not acknowledge business processing.
Applications define identities, request IDs, responses and retry policy.

CRTP `*_t<Derived>` endpoint types allow custom state while retaining transport
components. Keep state on its owning execution lane and synchronize cross-lane
access. See [architecture and extension](../project/architecture.md).

[Lifecycle](lifecycle.md) · [IO models](io-models.md) · [Sending](sending.md) · [Public types](../reference/api.md)
