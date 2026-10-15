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
| MySQL client | TODO; implement in arknet | The single-node scope is agreed; see Database clients below. Require independent-server interoperability and failure-path tests before ark plugin integration. |
| Redis client | TODO; implement in arknet | The single-node scope is agreed; see Database clients below. Require independent-server interoperability and failure-path tests before ark plugin integration. |
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

The first version targets single-node clients. Its scope is agreed, but it is not
yet released as an available capability. Implement the clients in arknet without
Boost.MySQL or Boost.Redis. arknet owns protocol communication and IO lifetime;
ark plugins integrate configuration, module services and business callbacks. The
C++ implementation remains header-only; TLS uses the existing OpenSSL dependency.

| Component | First-version scope |
| --- | --- |
| MySQL | Queries, parameterized prepared statements and transactions |
| Redis | Generic commands, RESP2/RESP3 and pipelining |
| Shared capabilities | Authentication, TLS, connection pools, timeouts, cancellation and queue limits |
| Later capabilities | Redis Cluster, Sentinel and Pub/Sub are excluded from this version |

Acceptance proceeds through codecs, single connections, pools and ark integration:

1. Codecs: fragmented input, combined replies, binary content, malformed lengths, nesting and memory limits.
2. Connections: independent MySQL/Redis server interoperability, authentication, TLS certificate validation, timeouts, cancellation, disconnects and reply ordering.
3. Pools: acquisition timeouts, full queues, draining on shutdown, concurrency and sustained-load tests. Record actual server versions and machine specifications.
4. Plugins: replace existing ark database calls only after client acceptance in arknet.

After Redis cancellation or timeout, already-sent replies must still be consumed,
or the connection closed, to preserve response correlation. MySQL prepared
statements and transactions belong to a connection; transactions retain a lease.
Never automatically replay a sent write; a disconnect during commit must preserve
an unknown-outcome result. Publish APIs after implementation and validation,
without placeholder interfaces.

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
