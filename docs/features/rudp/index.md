# RUDP

RUDP provides reliable, ordered messages over UDP. Its C++20 ARQ core handles
fragmentation, acknowledgements, retransmission and receive ordering. The public
client, server and session use arknet's CRTP and component architecture.

<Aside type="note" title="Available in v0.1.0">

This module is implemented and locally validated, and is included in `v0.1.0`.
[Policies](fec.md) include ARQ-only, fixed FEC and adaptive FEC. ARQ-only is the
default; peers negotiate FEC, while ARQ remains responsible for complete delivery.
Deployment capacity and endurance still require [production acceptance](../../project/production-readiness.md).
ARK integration additionally requires its business regression tests.

</Aside>

## Get started

- [Create a server](server.md): receive and echo complete messages.
- [Create a client](client.md): complete the handshake and verify an echo.
- [Configuration](configuration.md): MTU, windows, limits and compatibility.
- [FEC policies](fec.md): coding parameters, adaptive thresholds and statistics.
- [Performance](performance.md): local measurements and reproduction.

| Capability | Behavior |
| --- | --- |
| Endpoints | `rudp_client`, `rudp_server`, `rudp_session` and CRTP variants |
| Addresses and backend | IPv4/IPv6; Standalone Asio or Boost.Asio |
| Messages | Empty and binary messages; fragmentation and ordered reassembly |
| Reliability | ACKs, timeout and fast retransmission, bounded retries |
| Optional recovery | Fixed or adaptive Reed–Solomon FEC, with ARQ retransmission fallback |
| Flow control | Send/receive windows and congestion control enabled by default |
| Runtime | Handshake timeout, reconnect, timers, owned or external IO contexts |
| Limits | Bounds on ARQ, FEC, socket queues and server session admission |

## Choosing a transport

Use [native UDP](../udp/index.md) for independent datagrams whose loss and order
the application already handles. Use RUDP for reliable message delivery over
UDP. Retransmission adds latency; ordered delivery waits for missing fragments.
[TCP](../tcp/index.md) remains the standard choice for reliable byte streams.

RUDP uses KCP-compatible **message-mode segments** and a separate SYN/ACK/FIN
connection envelope. This does not make it interchangeable with a bare KCP
endpoint. It does not provide authentication, encryption, NAT traversal, stream
mode or automatic path-MTU discovery. See [compatibility](configuration.md#wire-compatibility).

A send completion confirms admission into the reliable queue, not a peer ACK
or application receipt. Disconnecting does not replay unacknowledged messages
into a new conversation. Applications that require receipt confirmation or
replay must define that behavior in their own protocol.
