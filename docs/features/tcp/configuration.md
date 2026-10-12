# TCP Configuration

The [client](client.md) and [server](server.md) examples provide complete startup, messaging and shutdown programs. Apply configuration before starting the relevant endpoint unless stated otherwise.

## Choose a Receive Policy

For a line protocol, start each receiver with `'\n'` and send the terminator:

```cpp
server.start("127.0.0.1", 7000, '\n');
client.start("127.0.0.1", 7000, '\n');
client.async_send(std::string("command\n"));
```

The callback includes `\n`. Multi-byte delimiters and custom Asio match conditions
are also accepted. Delimiters describe reads; they are not automatically added
by `async_send`. Without a receive policy, accumulate bytes in application
storage and parse all complete messages, retaining any unfinished suffix.
With `use_dgram`, do not add the length prefix yourself.

Configure a receive maximum with the constructor
`tcp_client(initial_bytes, maximum_bytes, workers)` or the equivalent server
constructor. A peer that never finishes a message must not be allowed to grow
the buffer without a bound. Set session idle limits with `set_silence_timeout`.

## Sending, Reconnect and Errors

Configure listeners and limits before `start`.

Use client `async_start(host, port, options...)` to begin connecting without
waiting. Its return value reports initiation; read the final result in
`bind_connect` through `get_last_error()` and send only after success.

This fragment belongs before the client's start call:

```cpp
client.set_connect_timeout(std::chrono::seconds(10));
client.set_auto_reconnect(true, std::chrono::seconds(1));
client.set_max_send_buffer_size(1024 * 1024);
client.bind_disconnect([]
{
    std::cerr << arknet::get_last_error().message() << '\n';
});
```

```cpp
client.async_send(std::string("request"),
    [](const arknet::error_code& error, std::size_t bytes)
    {
        if (error)
            std::cerr << error.message() << '\n';
        else
            std::cout << "written: " << bytes << '\n';
    });
```

Completions may instead accept `(bytes)` or `()`, but these signatures omit the
error argument. `asio::use_future` returns
`std::future<std::pair<arknet::error_code, std::size_t>>`; wait only outside IO
callbacks. Immediate rejection may invoke a completion on the submitting thread
before `async_send` returns. Accepted operations complete on the object's IO lane.
Use the completion's error argument; `get_last_error()` is thread-local.

The default send queue permits 16 MiB of payload and at most 1024 operations
per object. Admission failures report `no_buffer_space`. Observe queued payload
with `get_queued_send_buffer_size()` and pause producers when the queue is full.
Do not spin-retry in an IO callback. Old sends are not replayed after reconnect;
use application IDs and acknowledgments when a request may need a retry.

## Custom Sessions and External IO

```cpp
class peer_session : public arknet::tcp_session_t<peer_session>
{
public:
    using arknet::tcp_session_t<peer_session>::tcp_session_t;
    std::string user_id;
};

arknet::tcp_server_t<peer_session> server;
```

Keep session state on its IO lane or synchronize accesses from other lanes.
Copy a received view before retaining it or sending it to a CPU executor.
`async_send(bytes)` in the receive callback already owns the accepted payload.

For an external scheduler, construct a client or server with `asio::io_context&`.
Start its runners and retain a work guard before a blocking `start` call. A
server constructed with one context has one shared lane. Multiple entries in
`std::vector<asio::io_context*>` provide independent serialized lanes, even if
they point to one context. See [IO models](../../concepts/io-models.md) for complete rules.

Call `stop()` or `wait_stopped()` on the owner thread while the external context
still runs. In an IO callback, call `request_stop()` and arrange for the owner
thread to wait later. `wait_stopped()` rejects IO-worker calls. Stop and destroy
network objects before releasing the work guard and joining the host runners.

See [lifecycle tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/lifecycle.cpp),
[TCP tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/tcp.cpp) and
[performance](performance.md).
