# Create a HTTP Client

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the [matching server](server.md), then run this client in another terminal.

## HTTP Echo Client

The client sends `POST /echo` and checks both the HTTP status and echoed body.
It waits up to five seconds on the main thread and returns a nonzero status
if the exchange fails.

```cpp
#include <arknet/http/http_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <utility>

int main()
{
    std::promise<bool> reply;
    auto received = reply.get_future();
    bool received_once = false;
    arknet::http_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](arknet::http::response<arknet::http::string_body>& response)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(response.result() == arknet::http::status::ok &&
                response.body() == "hello HTTP");
        }
    });
    if (!client.start("127.0.0.1", 8080))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    arknet::http::request<arknet::http::string_body> request(
        arknet::http::verb::post, "/echo", 11);
    request.set(arknet::http::field::host, "127.0.0.1:8080");
    request.keep_alive(true);
    request.body() = "hello HTTP";
    request.prepare_payload();
    if (!client.async_send(std::move(request)))
        return 1;
    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    const bool correct = ready && received.get();
    client.stop();
    std::cout << (correct ? "echo received\n" : "echo failed\n");
    return correct ? 0 : 1;
}
```


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
