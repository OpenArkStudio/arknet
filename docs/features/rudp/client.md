# Create a RUDP client

Start the [echo server](server.md), then run this complete client program.

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

#include <arknet/rudp/rudp_client.hpp>

int main()
{
    std::promise<std::string> reply;
    auto received = reply.get_future();
    bool received_once = false;
    arknet::rudp_client client;
    client.set_auto_reconnect(false);
    client.set_connect_timeout(std::chrono::seconds(2));
    client.bind_recv([&](std::string_view message)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(std::string(message));
        }
    });
    if (!client.start("127.0.0.1", 7000))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    const std::string payload(4119, 'r');
    auto accepted = client.async_send(payload, asio::use_future);
    auto [error, bytes] = accepted.get();
    const bool ready = !error && bytes == payload.size() &&
        received.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    const bool matched = ready && received.get() == payload;
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

Unlike native UDP, `start()` waits for the remote RUDP handshake. `async_start()`
reports completion through `bind_connect`; inspect `get_last_error()` in that
callback. `bind_handshake` receives an `error_code` directly.

The future above confirms queue admission. The received echo confirms delivery
for this example. Retransmission has finite retries and is not a guarantee
during an outage. Configure [reconnect](../../guides/reconnect.md) according to
your application; a new connection has a new conversation and empty queues.
