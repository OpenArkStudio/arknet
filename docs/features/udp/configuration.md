# UDP Configuration

The [client](client.md) and [server](server.md) examples provide complete startup, messaging and shutdown programs. Apply configuration before starting the relevant endpoint unless stated otherwise.

## Cast to Several Destinations

This fragment binds a cast socket and echoes each datagram to its sender:

```cpp
#include <arknet/udp/udp_cast.hpp>

arknet::udp_cast socket;
socket.bind_recv([&](asio::ip::udp::endpoint& sender, std::string_view datagram)
{
    socket.async_send(sender, datagram);
});
socket.start("127.0.0.1", 0);
```

Send after start, using a numeric endpoint or asynchronous host/service resolution:

```cpp
auto target = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 7000);
socket.async_send(target, std::string("direct"),
    [](const arknet::error_code& error, std::size_t bytes)
    {
        if (error)
            std::cerr << error.message() << '\n';
    });
auto completion = socket.async_send("localhost", 7000, std::string("resolved"),
    asio::use_future);
```

The future returns `(error_code, bytes)`; do not wait inside IO callbacks.
Callbacks also accept `(bytes)` or `()`. Generic and move-only send callbacks
are supported. A resolution failure completes the same request with an error.
Copy a sender endpoint before storing it beyond the receive callback.

## Socket Options and Session State

UDP clients leave `SO_REUSEADDR` disabled by default so each client owns its
local endpoint. On Linux, enabling it can let connected UDP sockets share a
source port and misroute replies. Applications requiring reuse can explicitly
call `client.set_reuse_address(true)` in `bind_init`.

Configure native options in `bind_init`, when the socket has been opened. For
broadcast sending, configure before start:

```cpp
socket.bind_init([&]
{
    arknet::error_code error;
    socket.socket().set_option(asio::socket_base::broadcast(true), error);
    if (error)
        std::cerr << error.message() << '\n';
});
```

Bind an address suitable for the selected interface and send to the network's
broadcast endpoint. Multicast requires Asio's `asio::ip::multicast` options,
including group membership on the receiver and interface selection as needed.
The network and OS decide whether these packets are routed or accepted.
Socket-buffer options do not change UDP reliability or the application's send
queue. Inspect actual buffer sizes if measuring their effect.

Define per-peer state through `udp_session_t<Derived>` and use
`udp_server_t<Session>` for the server. Configure idle expiry before starting:

```cpp
server.bind_connect([](auto& session)
{
    session->set_silence_timeout(std::chrono::seconds(30));
});
```

The first datagram is delivered after the connect callback. The session key is
the source IP/port, not an authenticated identity. Applications decide how to
handle duplicate, reordered or missing messages and changing source endpoints.

## Ownership, Limits and Shutdown

Views received by server, client or cast callbacks are borrowed. Accepted sends
copy or own input bytes and keep their destination alive until completion.
`async_send` returns queue acceptance, not peer receipt. Immediate rejection may
call its completion on the submitting thread; accepted sends complete on the
IO lane. Read errors from the completion argument or the relevant lifecycle
callback because `get_last_error()` is thread-local.

Send queues default to 16 MiB and 1024 operations per object; configure the byte
limit with `set_max_send_buffer_size`. Rejections report `no_buffer_space`.
Receive maxima must be at least 65536 bytes. Oversized sends may report the OS's
`message_size` or another native socket error; Windows may return system error
1784 for an oversized datagram. The completion preserves that error and reports
zero bytes. Do not retry the same size indefinitely.

Construct endpoints with an external `asio::io_context&` when hosting the runners
yourself. All sessions of one UDP server share the socket's serialized lane.
Keep the context running until endpoints stop. Call `request_stop()` in callbacks
and `stop()` or `wait_stopped()` on the owner thread, then destroy endpoints before
stopping the host context. See [IO models](../../concepts/io-models.md),
[UDP regression tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/udp.cpp)
and [performance](performance.md).
