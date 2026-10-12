# Create a TCP Client

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the [matching server](server.md), then run this client in another terminal.

## Echo Client

The client sends one message, verifies the reply and exits. It returns a nonzero
status if connecting, queue admission or the five-second echo check fails.

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

#include <arknet/tcp/tcp_client.hpp>

int main()
{
    std::promise<std::string> reply;
    auto received = reply.get_future();
    bool received_once = false;

    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view bytes)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(std::string(bytes));
        }
    });
    if (!client.start("127.0.0.1", 7000, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("hello TCP")))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    const bool matched = ready && received.get() == "hello TCP";
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
