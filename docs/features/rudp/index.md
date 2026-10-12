# RUDP

RUDP provides reliable, ordered messages over UDP. Its C++20 ARQ core handles
fragmentation, acknowledgements, retransmission and receive ordering. The public
client, server and session use arknet's CRTP and component architecture.

This module is experimental and is not included in `v0.1.0-rc.1`.
[Optional FEC](../../project/roadmap.md) is planned as a component combined with
ARQ; the current implementation and measurements do not include it.

## Get started

- [Create a server](server.md): receive and echo complete messages.
- [Create a client](client.md): complete the handshake and verify an echo.
- [Configuration](configuration.md): MTU, windows, limits and compatibility.
- [Performance](performance.md): local measurements and reproduction.

| Capability | Behavior |
| --- | --- |
| Endpoints | `rudp_client`, `rudp_server`, `rudp_session` and CRTP variants |
| Addresses and backend | IPv4/IPv6; Standalone Asio or Boost.Asio |
| Messages | Empty and binary messages; fragmentation and ordered reassembly |
| Reliability | ACKs, timeout and fast retransmission, bounded retries |
| Flow control | Send/receive windows and congestion control enabled by default |
| Runtime | Handshake timeout, reconnect, timers, owned or external IO contexts |
| Limits | Per-direction ARQ memory budget and bounded socket-send queue |

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
