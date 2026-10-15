# Roadmap and TODO

The current implementation includes TCP, UDP/cast, WebSocket, HTTP/1.0 and
HTTP/1.1, RUDP with ARQ and optional FEC, routing, timers/reconnect and optional TLS/HTTPS/WSS/mTLS. Planned
capabilities below have no public placeholder API and no release date.

For the implemented capabilities, see the [local performance report](../performance/overview.md)
for throughput and p99 charts, model comparisons and machine specifications.

## Protocol and Transport Status

| Capability | Status | Work and acceptance criteria |
| --- | --- | --- |
| RUDP FEC | Included in v0.1.0 | ARQ-only, fixed and adaptive Reed–Solomon policies, versioned negotiation, MTU accounting, bounded decoding and group expiry. FEC requires compatible peers; legacy ARQ sessions remain supported. See [policies](../features/rudp/fec.md) and [production acceptance](production-readiness.md). |
| HTTP/2 | TODO; dependency selection pending | Client/server/session, TLS ALPN `h2`, h2c prior knowledge, HPACK, multiplexing, stream/connection flow control, bounded headers/bodies, cancellation and GOAWAY. Verify interoperability with an independent peer and slow-consumer tests. |
| HTTP/3 | TODO; research | Select a QUIC transport and QPACK implementation; require TLS 1.3, stream limits, congestion/loss handling, migration policy and independent-peer interoperability. |
| Named pipe | TODO | Windows local client/server, message/byte semantics, overlapped IO, access control, cancellation and restart. |
| Unix domain socket | TODO | Local stream client/server, path ownership and cleanup, permission policy, cancellation and restart. Platform-specific abstract namespace/datagrams require separate decisions. |
| RPC | TODO | Transport-independent framing and codec selection, request IDs, deadlines, cancellation, bounded inflight work and error propagation. |
| MySQL client | TODO; implement in arknet | [Single-node scope and acceptance](../features/database-clients.md); independent-server interoperability and failure-path tests before ark plugin integration. |
| Redis client | TODO; implement in arknet | [Single-node scope and acceptance](../features/database-clients.md); independent-server interoperability and failure-path tests before ark plugin integration. |
| Public coroutine API | TODO | Awaitable connect/read/write and cancellation integrated with lifecycle; compare shared and sharded executors under IO and CPU work. |
| WebSocket compression | TODO | Negotiate permessage-deflate; enforce decompressed limits and test adversarial frames. |
| DTLS | TODO | Select a supported engine; define peer identity, retransmission and datagram limits. |

## HTTP Engine Boundaries

[Boost.Beast](https://github.com/boostorg/beast#introduction) implements
**HTTP/1 and WebSocket**, not HTTP/2 or HTTP/3. An HTTP version field does not
add HTTP/2 framing or HTTP/3 transport.

nghttp2 is a candidate HTTP/2 protocol engine; enabling it would require an
optional compiled dependency while arknet's C++ wrapper remains header-only.
No nghttp2 dependency is added in this release. HTTP/3 needs a QUIC/QPACK
stack; its engine and dependency model remain undecided. Existing Beast
HTTP/1 transports remain independently usable.

## Database clients

MySQL queries, prepared statements and transactions, together with Redis
RESP2/RESP3 commands and pipelines, form the agreed single-node scope. Both
clients require authentication, TLS, pools, timeouts, cancellation and queue
limits. Redis Cluster, Sentinel and Pub/Sub follow later.

The [database client guide](../features/database-clients.md) defines ownership,
connection-bound operations, uncertain write outcomes, validation and performance
report requirements. The clients remain planned until that acceptance is complete;
ark plugin integration follows client validation. No placeholder API is published.

## Future Scope

[RUDP](../features/rudp/index.md) and its C++20 ARQ core are included in `v0.1.0`.
[FEC policies](../features/rudp/fec.md) describe the fixed and adaptive presets,
activation thresholds and resource limits.
The release covers KCP message-mode interoperability, retry/queue limits and local
measurement. ARK's RDP workflow now uses arknet through [ARK PR #38](https://github.com/OpenArkStudio/ARK/pull/38);
production-duration validation remains to be verified for deployment adoption.

MQTT and proxies are not scheduled. Add them only when a concrete consumer and
acceptance criteria exist. ARK integration proceeds through
a direct caller replacement with business regression tests; production deployment
requires [deployment acceptance](production-readiness.md).
