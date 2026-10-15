# Public API Index

Include `<arknet/arknet.hpp>` for enabled transport capabilities, or include only
the specific public header. Database clients use their own explicit headers.
Secure transport headers and MySQL connections require `ARKNET_ENABLE_SSL`.

## Public Types

| Family | Client | Server / session | Guide |
| --- | --- | --- | --- |
| TCP | `tcp_client`, `tcp_client_t<Derived>` | `tcp_server`, `tcp_server_t<Session>`, `tcp_session_t<Derived>` | [TCP](../features/tcp/index.md) |
| UDP | `udp_client` | `udp_server`, `udp_session`; independent `udp_cast` | [UDP](../features/udp/index.md) |
| RUDP | `rudp_client`, `rudp_client_t<Derived>` | `rudp_server`, `rudp_server_t<Session>`, `rudp_session_t<Derived>` | [RUDP](../features/rudp/index.md) |
| ARQ core | `arq::control_block`, `arq::options` | No Asio dependency; serialized calls required | [ARQ configuration](../features/rudp/configuration.md) |
| Single HTTP request | `http_request`, `http_request_options` | Callback or future completion; cancel and deadlines | [Requests](../features/http/requests.md) |
| WebSocket | `ws_client` | `ws_server`, `ws_session` | [WebSocket](../features/websocket/index.md) |
| HTTP | `http_client` | `http_server`, `http_session`, `http_router` | [HTTP](../features/http/index.md), [routing](../guides/http-routing.md) |
| TLS TCP | `tcps_client` | `tcps_server`, `tcps_session` | [TLS](../features/tls/index.md) |
| HTTPS | `https_client` | `https_server`, `https_session` | [HTTP](../features/http/index.md) |
| Secure WebSocket | `wss_client` | `wss_server`, `wss_session` | [TLS](../features/tls/index.md) |
| MySQL | `mysql::connection`, `mysql::pool` | Exclusive pool leases; explicitly include `<arknet/mysql/connection.hpp>` or `<arknet/mysql/pool.hpp>` | [MySQL](../features/mysql/index.md) |
| Scheduling | `timer`, `io_pool`, `io_t` | `iopool` is an alias for `io_pool` | [Runtime](../concepts/lifecycle.md), [IO models](../concepts/io-models.md) |

The protocol guides cover CRTP variants and public include paths. UDP endpoint
sessions share the server datagram socket. HTTP request/response types live
in `arknet::http`; WebSocket protocol types live in `arknet::websocket`.

## Common Operations

| Area | API | Contract |
| --- | --- | --- |
| Start | `start(host, port, ...)`, client `async_start(...)` | Inspect returned success and `get_last_error()`; async start completion is reported by listeners. |
| Listeners | `bind_recv`, `bind_connect`, `bind_disconnect`, `bind_start`, `bind_stop` | Callback signatures depend on client/server and protocol. Bind before starting. |
| Sending | `async_send(data, callback)`, future overload, `send` | Queue admission and write completion are distinct. See [ownership and threading](../concepts/sending.md). |
| Shutdown | `request_stop`, `wait_stopped`, `stop` | Network objects remain alive until shutdown completes. IO callbacks request shutdown; owners wait. |
| Limits | `set_max_send_buffer_size`, `get_queued_send_buffer_size` | Reject overload with `no_buffer_space`; defaults include a 16 MiB / 1024-operation quota. |
| HTTP limits | `set_http_header_limit`, `set_http_body_limit` | Default 8192-byte headers, 16 MiB bodies; new limits take effect with the next parser. |
| Scheduling | `post`, `dispatch`, `start_timer`, `stop_timer` | Execute through the object's IO lane. Do not block the worker. |
| Error state | `arknet::get_last_error()` | Thread-local; inspect on the relevant calling/callback thread before another operation changes it. |

## Extension and Source Reference

Custom clients/sessions derive from the protocol's `*_t<Derived>`.
Server templates accept the session type. See [architecture](../project/architecture.md) and
the permanent [tests](https://github.com/OpenArkStudio/arknet/tree/main/tests).
Implementation headers under `base/impl` and `detail` are not stable public APIs.
