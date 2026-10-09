# TCP

arknet provides TCP clients, servers and per-connection sessions for persistent,
ordered byte streams. Use it for service connections, device protocols and
application protocols whose framing is under application control. Encrypted TCP
uses the same model through [TLS](../tls/overview.md).

## Objects and Extension

| Type | Responsibility |
| --- | --- |
| `tcp_client` | Resolve, connect, receive, send and optionally reconnect to one peer |
| `tcp_server` | Listen, create sessions, report lifecycle events and stop its sessions |
| `tcp_session` | Hold one accepted connection and its timers, state and send queue |
| `tcp_client_t<Derived>` / `tcp_session_t<Derived>` | Extend endpoints with CRTP while keeping the transport components |
| `tcp_server_t<Session>` | Construct an application-defined session type |

Listeners cover initialization, connection, disconnection and receive events.
Servers additionally expose accept, start and stop events. Components provide
timers, `post`, user data, connection timeouts, idle timeouts for sessions,
automatic client reconnect and bounded asynchronous sending.

## Message Boundaries

TCP preserves bytes and their order; it does not preserve individual sends.
One receive callback can contain part of a send or several sends together.

| Receive policy passed to `start` | Application contract |
| --- | --- |
| No policy | Deliver available stream bytes; application reconstructs messages |
| A delimiter such as `'\n'` or `"\r\n"` | Deliver through the delimiter, including it |
| An Asio completion condition such as `asio::transfer_exactly(n)` | Deliver after the requested amount has been read |
| An Asio match condition | Application-defined complete-message detection |
| `arknet::use_dgram` | Add and remove arknet's length prefix; both peers must use this policy |

`use_dgram` remains reliable TCP, despite its name. Its prefix is one byte for
payload lengths below 254, marker `254` followed by a two-byte little-endian
length up to 65535, or marker `255` followed by an eight-byte little-endian
length. Empty frames are supported. This is a protocol choice, not a mode that
can be enabled against an arbitrary TCP service. Receive-buffer limits also
apply to the framed message and prefix.

## Ownership and Scheduling

Received `std::string_view` data is borrowed for the callback duration. Accepted
`async_send` operations own their payload until completion, including views,
spans and Asio buffers. The return value reports queue acceptance; a successful
write completion does not mean that the peer application processed the message.

Owned IO pools run one worker per context. External contexts may have one or
several runners and use serialized lanes. Passing one external context directly
to a server gives its sessions one shared lane; supply multiple lanes to allow
independent sessions to run concurrently. See [IO models](../threading.md).

Applications still define request IDs, acknowledgments, deadlines, retry policy
and authentication. Client reconnect does not replay old sends or establish
exactly-once application processing.

## Next Steps

- [Usage](usage.md): echo, framing, custom sessions and shutdown.
- [Performance](performance.md): reproduce IO-model and workload comparisons.
- [TCP tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/tcp.cpp): buffer ownership, framing boundaries and external-context shutdown.
