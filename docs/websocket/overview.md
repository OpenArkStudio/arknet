# WebSocket

arknet provides WebSocket clients, servers and sessions for persistent,
bidirectional messages. Use them for browser connections, chat, notifications
and control channels where both peers need to send without polling.
`wss_client`, `wss_server` and `wss_session` add [TLS](../tls/overview.md).

## Protocol and Objects

| Type | Responsibility |
| --- | --- |
| `ws_client` | TCP connect, HTTP upgrade, message IO and optional reconnect |
| `ws_server` / `ws_session` | Accept TCP connections, upgrade and handle individual peers |
| `ws_client_t<Derived>` / `ws_session_t<Derived>` | CRTP extension points |
| `ws_server_t<Session>` | Application-defined session type |
| `ws_stream()` | Native Beast stream for protocol configuration and inspection |

The implementation uses Beast: bundled headers adapted for standalone Asio,
or official Boost.Beast with Boost.Asio. Both providers expose the same arknet
endpoint API. See [provider selection](../guide.md#choose-an-asio-provider).

The client upgrade target defaults to `/` and can include a path and query.
Client responses and server requests remain available through
`get_upgrade_response()` and `get_upgrade_request()`. `bind_upgrade` reports
upgrade completion; connect callbacks represent a usable WebSocket connection
after successful upgrade. WSS performs TLS handshake before HTTP upgrade.

Receive callbacks deliver complete WebSocket messages; Beast reassembles
fragmented messages. Text frames are the default. Binary mode preserves
arbitrary bytes, while text messages must contain valid UTF-8. `got_binary()`
reports the current received message type. Ping/pong and close frames are
handled by the protocol stream and are not delivered as application messages.
WebSocket compression is deferred from the supported feature set.

## Ownership and Application Policy

Receive views are valid only during the callback. Accepted sends own payloads
until completion. Queue acceptance and write completion do not prove that the
peer application processed a message. Use application IDs and acknowledgments
where business processing needs confirmation.

Timers, posting, bounded send queues, connection timeouts, idle session limits
and client reconnect follow the TCP lifecycle. Reconnect recreates the stream
and repeats the upgrade; initialization/decorator configuration should be
applied in the appropriate lifecycle callback. Old messages are not replayed.

These are dedicated WebSocket endpoints. They do not provide an HTTP router,
browser-origin authorization, application login or automatic subprotocol
selection. Define these policies in application logic. Use the separate
HTTP endpoints when serving ordinary HTTP messages.

## Scheduling and Shutdown

Socket and protocol operations run on serialized IO lanes. Configure native
stream options during client init/connect or server accept, or on its lane.
Bypassing arknet's send queue with overlapping native writes or closes breaks
the operation ordering contract.

Owned pools and external contexts follow [IO models](../threading.md). A server
with one external lane serializes its sessions; multiple lanes permit
independent sessions to run concurrently. Shutdown attempts a WebSocket close
handshake and bounds it with the disconnect timeout before transport cleanup.
Keep objects and any external runners alive until stop completes.

## Next Steps

- [Usage](usage.md): binary echo, upgrade decorators, limits and custom sessions.
- [Performance](performance.md): binary-message benchmarks across IO models.
- [WebSocket tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/websocket.cpp): text/binary messages, upgrade paths and custom endpoints.
