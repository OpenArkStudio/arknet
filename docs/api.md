# Public API Index

Include `<arknet/arknet.hpp>` for all enabled capabilities, or include only the
specific public header. Secure headers require `ARKNET_ENABLE_SSL`.

## Public Types

| Family | Client | Server / session | Guide |
| --- | --- | --- | --- |
| TCP | `tcp_client`, `tcp_client_t<Derived>` | `tcp_server`, `tcp_server_t<Session>`, `tcp_session_t<Derived>` | [TCP](tcp/usage.md) |
| UDP | `udp_client` | `udp_server`, `udp_session`; independent `udp_cast` | [UDP](udp/usage.md) |
| WebSocket | `ws_client` | `ws_server`, `ws_session` | [WebSocket](websocket/usage.md) |
| HTTP | `http_client` | `http_server`, `http_session`, `http_router` | [HTTP](http/usage.md), [routing](http/routing.md) |
| TLS TCP | `tcps_client` | `tcps_server`, `tcps_session` | [TLS](tls/usage.md) |
| HTTPS | `https_client` | `https_server`, `https_session` | [HTTP](http/usage.md) |
| Secure WebSocket | `wss_client` | `wss_server`, `wss_session` | [TLS](tls/usage.md) |
| Scheduling | `timer`, `io_pool`, `io_t` | `iopool` is an alias for `io_pool` | [Runtime](runtime.md), [IO models](threading.md) |

The protocol guides cover CRTP variants and public include paths. UDP endpoint
sessions share the server datagram socket. HTTP request/response types live
in `arknet::http`; WebSocket protocol types live in `arknet::websocket`.

## Common Operations

| Area | API | Contract |
| --- | --- | --- |
| Start | `start(host, port, ...)`, client `async_start(...)` | Inspect returned success and `get_last_error()`; async start completion is reported by listeners. |
| Listeners | `bind_recv`, `bind_connect`, `bind_disconnect`, `bind_start`, `bind_stop` | Callback signatures depend on client/server and protocol. Bind before starting. |
| Sending | `async_send(data, callback)`, future overload, `send` | Queue admission and write completion are distinct. See [ownership and threading](runtime.md). |
| Shutdown | `request_stop`, `wait_stopped`, `stop` | Network objects remain alive until shutdown completes. IO callbacks request shutdown; owners wait. |
| Limits | `set_max_send_buffer_size`, `get_queued_send_buffer_size` | Reject overload with `no_buffer_space`; defaults include a 16 MiB / 1024-operation quota. |
| HTTP limits | `set_http_header_limit`, `set_http_body_limit` | Default 8192-byte headers, 16 MiB bodies; new limits take effect with the next parser. |
| Scheduling | `post`, `dispatch`, `start_timer`, `stop_timer` | Execute through the object's IO lane. Do not block the worker. |
| Error state | `arknet::get_last_error()` | Thread-local; inspect on the relevant calling/callback thread before another operation changes it. |

## Extension and Source Reference

Custom clients/sessions derive from the protocol's `*_t<Derived>`.
Server templates accept the session type. See [architecture](design.md) and
the permanent [tests](https://github.com/OpenArkStudio/arknet/tree/main/tests).
Implementation headers under `base/impl` and `detail` are not stable public APIs.
