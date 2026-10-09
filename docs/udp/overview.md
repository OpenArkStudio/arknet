# UDP

arknet provides ordinary UDP datagrams through a connected client, an
endpoint-session server and an unconnected `udp_cast` socket. Use UDP for
discovery, telemetry or state updates whose application protocol can tolerate
or detect loss. Message boundaries are preserved, including empty datagrams
and binary payloads with embedded null bytes.

## Choose an Endpoint

| Type | Model | Suitable use |
| --- | --- | --- |
| `udp_client` | One OS-connected UDP socket with a selected peer | Send and receive with one service |
| `udp_server` / `udp_session` | One bound socket; application sessions keyed by source IP and port | Per-peer state and replies |
| `udp_cast` | One bound, unconnected socket; every receive exposes its sender | Discovery and communication with multiple destinations |
| `udp_client_t<Derived>`, `udp_session_t<Derived>`, `udp_cast_t<Derived>` | CRTP extensions using the same components | Application state and receive/send filtering |
| `udp_server_t<Session>` | Application-defined session type | Extend per-endpoint state |

The server creates a session when an endpoint first sends a datagram. Its
connect event is a local session event, not a network handshake. Idle timers
can expire session state; a later datagram can create a new session. A NAT
mapping or source-port change produces a different endpoint identity.
Stopping one session does not close the server's shared socket.

Clients retain timers, posting, reconnect policy and bounded sends. Cast sockets
send directly to an Asio endpoint or resolve a host/service for a send.
Broadcast and multicast require explicit native socket options and appropriate
network/interface configuration; `udp_cast` is not an automatic discovery or
group-membership service.

## Delivery and Size Limits

UDP does not guarantee delivery, ordering, deduplication, peer availability or
congestion control. A successful `start` on a client selects a destination; it
does not prove a server is listening. A send completion reports a local socket
operation, not a remote receipt. Automatic client reconnect does not retransmit
lost datagrams.

Receivers reserve 65536 bytes per receive and reject a configured receive
maximum below 65536 at start. This avoids truncating the first ordinary
datagram because of a small initial buffer. OS limits and network MTU still
apply. The IPv4 theoretical payload maximum is 65507 bytes, but smaller OS
limits may produce `message_size`, and large packets may need IP fragmentation.
Prefer payloads appropriate to the deployed network.

Received views and cast sender references are valid only during their callback.
Accepted asynchronous sends own both payload and destination until completion.
Send queues have byte and operation limits; admission is separate from delivery.

## Scheduling and Deferred Features

All sessions of one UDP server share its socket and serialized IO lane. Several
threads running the host context do not execute that server's receive callbacks
concurrently. Independent clients, cast sockets or servers can use different
lanes. Keep callbacks short and synchronize shared application state.

This release does not implement KCP or DTLS. Reliability, fragmentation above
UDP, authentication, replay prevention and encryption need an application
protocol or a future transport. See [IO models](../threading.md).

## Next Steps

- [Usage](usage.md): echo, cast destinations, socket options and shutdown.
- [Performance](performance.md): paced traffic, loss checks and IO-model comparison.
- [UDP tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/udp.cpp): empty/binary payloads, endpoint sessions, restart, ownership and concurrent admission.
