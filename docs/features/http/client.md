# Create a HTTP Client

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the [matching server](server.md), then run this client in another terminal.

## Domain Names and Default Ports

`http_client.start(host)` and `async_start(host)` default to port **80**;
the same overloads on `https_client` default to **443**. An explicit port overrides
the default. The host accepts a domain name or an unbracketed IPv4/IPv6 literal,
not a full URL.

```cpp
arknet::http_client client;
client.start("example.com");          // HTTP, port 80
// Or: client.start("example.com", 8080);
```

With TLS enabled, use the separate HTTPS client type. Configure trusted CAs before
connecting; certificate chains, DNS identity and SNI follow [TLS configuration](../tls/configuration.md):

```cpp
arknet::https_client client;
client.load_verify_file("ca.pem");
client.start("example.com");          // HTTPS, port 443
```

Requests sent by persistent clients still need a `Host` field. Use `example.com`
for the default port and `example.com:8080` for a non-default port; IPv6 uses
`[::1]` and `[::1]:8080`, respectively.

## HTTP Echo Client

The client sends `POST /echo` and checks both the HTTP status and echoed body.
It waits up to five seconds on the main thread and returns a nonzero status
if the exchange fails.

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <utility>

#include <arknet/http/http_client.hpp>

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
