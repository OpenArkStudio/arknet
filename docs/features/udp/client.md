# Create a UDP Client

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the [matching server](server.md), then run this client in another terminal.

## Echo Client

The client associates its local UDP socket with the server and sends a binary
datagram. Its main thread waits up to five seconds for the reply; this timeout is
not a UDP delivery guarantee. Additional datagrams cannot fulfill the same
promise twice.

```cpp
#include <arknet/udp/udp_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

int main()
{
    std::promise<std::string> reply;
    auto received = reply.get_future();
    bool received_once = false;

    arknet::udp_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view datagram)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(std::string(datagram));
        }
    });
    if (!client.start("127.0.0.1", 7000))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("a\0b", 3)))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    const bool matched = ready && received.get() == std::string("a\0b", 3);
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

Use `std::string{}` for an empty datagram. Use a sized string, view, span or buffer
for binary data; a bare character pointer uses a null-terminated string length.
Do not pass a null pointer even for an otherwise empty pointer/count request.

Client `async_start(host, port)` initiates resolution and local socket connection
without waiting; inspect `get_last_error()` in `bind_connect` for completion.
Cast `async_start(local_host, local_port)` instead reports completion through
`bind_start`. Neither operation performs a remote UDP handshake.


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
