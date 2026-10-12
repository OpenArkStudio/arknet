# Roadmap and TODO

The current implementation includes TCP, UDP/cast, WebSocket, HTTP/1.0 and
HTTP/1.1, routing, timers/reconnect and optional TLS/HTTPS/WSS/mTLS. Planned
capabilities below have no public placeholder API and no release date.

For the implemented capabilities, see the [local performance report](../performance/overview.md)
for throughput and p99 charts, model comparisons and machine specifications.

## Protocol and Transport TODO

| Capability | Status | Work and acceptance criteria |
| --- | --- | --- |
| RUDP FEC | TODO; optional component | Combine forward error correction with the existing ARQ core, disabled by default. Define versioned group/shard headers, MTU accounting, bounded decoding memory and group deadlines. Compare loss/burst recovery, p99 latency, bandwidth overhead and CPU with ARQ alone. FEC framing requires compatible peers and does not preserve bare KCP wire interoperability. |
| HTTP/2 | TODO; dependency selection pending | Client/server/session, TLS ALPN `h2`, h2c prior knowledge, HPACK, multiplexing, stream/connection flow control, bounded headers/bodies, cancellation and GOAWAY. Verify interoperability with an independent peer and slow-consumer tests. |
| HTTP/3 | TODO; research | Select a QUIC transport and QPACK implementation; require TLS 1.3, stream limits, congestion/loss handling, migration policy and independent-peer interoperability. |
| Named pipe | TODO | Windows local client/server, message/byte semantics, overlapped IO, access control, cancellation and restart. |
| Unix domain socket | TODO | Local stream client/server, path ownership and cleanup, permission policy, cancellation and restart. Platform-specific abstract namespace/datagrams require separate decisions. |
| RPC | TODO | Transport-independent framing and codec selection, request IDs, deadlines, cancellation, bounded inflight work and error propagation. |
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

## Future Scope

[RUDP](../features/rudp/index.md) and its C++20 ARQ core are now experimental.
[FEC design](../features/rudp/fec.md) records the planned configurable activation
conditions; the overall policy and presets remain undecided.
KCP message-mode interoperability, retry/queue limits and local measurement
are implemented; ARK's full RDP workflow and production-duration validation
remain to be verified. These additions are not in `v0.1.0-rc.1`.

MQTT, database protocols and proxies are not scheduled. Add them only when a
concrete consumer and acceptance criteria exist. ARK adoption follows complete
arknet validation; it is not part of this rewrite.
