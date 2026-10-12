# WebSocket Configuration

The [client](client.md) and [server](server.md) examples provide complete startup, messaging and shutdown programs. Apply configuration before starting the relevant endpoint unless stated otherwise.

## Secure WebSocket (WSS)

With `ARKNET_ENABLE_SSL=ON`, replace the server header/type with
`<arknet/websocket/wss_server.hpp>` / `arknet::wss_server`, and the client
header/type with `<arknet/websocket/wss_client.hpp>` / `arknet::wss_client`.
Load the server certificate/key and the client CA before start using the
[TLS server and client examples](../tls/configuration.md). Connect with `localhost` when
using the test certificate, and keep the `/echo` target. Server start still takes
only the listening host and port; neither WSS endpoint uses `use_dgram`.

## Configure Upgrade Headers and Inspect the Result

Before starting the endpoints, register decorators in lifecycle callbacks so
they are reapplied to streams recreated by reconnect:

```cpp
client.bind_init([&]
{
    client.ws_stream().set_option(websocket::stream_base::decorator(
        [](websocket::request_type& request)
        {
            request.set(http::field::user_agent, "arknet-example");
        }));
});
server.bind_accept([](auto& session)
{
    session->ws_stream().set_option(websocket::stream_base::decorator(
        [](websocket::response_type& response)
        {
            response.set(http::field::server, "arknet-example");
        }));
});
server.bind_upgrade([](auto& session)
{
    if (!arknet::get_last_error())
        std::cout << session->get_upgrade_request().target() << '\n';
});
```

Use `client.start(host, port, "/path?key=value")` or set the default target with
`set_upgrade_target`. After successful start, `get_upgrade_response()` exposes
the HTTP upgrade response. Upgrade callbacks report completion, including
errors; they are not a pre-upgrade HTTP routing/authentication API. Define
origin, credential and subprotocol policy before treating a peer as authorized.

## Limits, Sessions and Shutdown

The constructor's receive maximum also becomes the native stream's maximum
message size; the default is 16 MiB. Configure message and send limits before
start. A send queue defaults to 16 MiB of payload and 1024 operations; adjust
its byte limit with `set_max_send_buffer_size`. Admission failure reports
`no_buffer_space`. `get_queued_send_buffer_size()` observes queued payload bytes.

`async_send` owns accepted payloads. Its boolean return is queue acceptance;
completion reports a local write, not application delivery. Completions accept
`(error_code, bytes)`, `(bytes)` or `()`, and `asio::use_future` returns the
error/byte pair in a future. An immediate failure may invoke completion on the
submitting thread. Accepted sends complete on the serialized lane. Copy received
views before retaining them; do not wait for send futures in IO callbacks.

Extend sessions with `ws_session_t<MySession>` and use
`ws_server_t<MySession>`. Extend clients with `ws_client_t<MyClient>`.
The same scheme is available through `wss_session_t`, `wss_server_t` and
`wss_client_t` when TLS is enabled. Configure state on its owning lane; multiple
external lanes may execute concurrently.

Automatic reconnect recreates the protocol stream and does not replay pending
business messages. Set `set_auto_reconnect(false)` when the application owns
retry policy. Configure connection and disconnect deadlines with
`set_connect_timeout` and `set_disconnect_timeout`.

From an IO callback, use `request_stop()` and wait later on the owner thread
with `wait_stopped()` or `stop()`. Shutdown attempts the close handshake and
then transport cleanup. Stop and destroy endpoints while any external context
still runs; only then stop its runners. See [IO models](../../concepts/io-models.md),
[tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/websocket.cpp)
and [performance](performance.md).
