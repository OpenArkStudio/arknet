# Create a WebSocket Client

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the [matching server](server.md), then run this client in another terminal.

## Binary Echo Client

The client upgrades at `/echo`, sends one binary message and checks both its
content and type. Its main thread waits for the reply; IO callbacks do not block.

```cpp
#include <arknet/websocket/ws_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

int main()
{
    std::promise<std::pair<std::string, bool>> reply;
    auto received = reply.get_future();
    bool received_once = false;

    arknet::ws_client client;
    client.set_auto_reconnect(false);
    client.bind_connect([&]
    {
        if (!arknet::get_last_error())
            client.ws_stream().binary(true);
    });
    client.bind_recv([&](std::string_view bytes)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value({std::string(bytes), client.ws_stream().got_binary()});
        }
    });
    if (!client.start("127.0.0.1", 7001, "/echo"))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("a\0b", 3)))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    bool matched = false;
    if (ready)
    {
        auto [bytes, binary] = received.get();
        matched = binary && bytes == std::string("a\0b", 3);
    }
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

For text messages, leave the default mode or set `ws_stream().text(true)` on
the IO lane before sending. Text payloads must be valid UTF-8. Use
`std::string{}` for an empty message and a sized string/view/buffer for binary
payloads. The receive callback runs once per assembled message, not per frame.

Client `async_start(host, port, target)` begins connecting and upgrading without
waiting. Its return reports initiation; `bind_upgrade` reports the upgrade
result, and a successful `bind_connect` indicates a usable endpoint. Read
`get_last_error()` in the relevant callback before sending.


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
