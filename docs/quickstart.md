# First Client and Server

[Installation](guide.md)

This complete program starts a loopback TCP echo server and a client, verifies
a reply, and shuts both down. Follow [installation](guide.md) to install dependencies
and use the `arknet::arknet` CMake interface target. arknet is header-only;
this target propagates build settings and dependencies without an arknet binary library.

```cpp
#include <arknet/arknet.hpp>
#include <chrono>
#include <future>
#include <string>
using namespace std::chrono_literals;

int main()
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data)
    {
        session->async_send(data);
    });
    if (!server.start("127.0.0.1", 0, arknet::use_dgram))
        return 1;

    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    std::promise<std::string> reply;
    client.bind_recv([&](std::string_view data)
    {
        reply.set_value(std::string(data));
    });
    if (!client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram))
        return 2;
    auto result = reply.get_future();
    if (!client.async_send("hello"))
        return 3;
    const bool ready = result.wait_for(5s) == std::future_status::ready;
    const bool correct = ready && result.get() == "hello";
    client.stop();
    server.stop();
    return correct ? 0 : 4;
}
```

Both peers use arknet's `use_dgram` message framing. TCP itself is a byte
stream: choose [framing](tcp/usage.md) for the peer's wire protocol. The example
waits on the owner thread, outside IO callbacks.

## Next Steps

Choose [TCP](tcp/usage.md), [UDP](udp/usage.md),
[WebSocket](websocket/usage.md), [HTTP and routing](http/usage.md) or
[TLS](tls/usage.md). Read [lifecycle and sends](runtime.md) before keeping
objects in a service, and [IO models](threading.md) before sharing a context.
