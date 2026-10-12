# Create a HTTP Server

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the server before running the [matching client](client.md).

## HTTP Echo Server

Use the `arknet::arknet` interface target in a C++20 application following the
[build guide](../../get-started/installation.md). Build the server and client blocks as separate
executables. Start the server first, then run the client in another terminal.
Press Enter in the server terminal to stop it. Plain HTTP does not require TLS.

```cpp
#include <arknet/http/http_server.hpp>
#include <iostream>
#include <utility>

int main()
{
    arknet::http_server server;
    server.bind_recv([](auto& session, arknet::http::request<arknet::http::string_body>& request)
    {
        arknet::http::response<arknet::http::string_body> response(
            arknet::http::status::ok, request.version());
        response.keep_alive(request.keep_alive());
        response.set(arknet::http::field::content_type, "text/plain");
        response.body() = request.body();
        response.prepare_payload();
        session->async_send(std::move(response));
    });
    if (!server.start("127.0.0.1", 8080))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "HTTP listening on http://127.0.0.1:8080/echo\n";
    std::cin.get();
    server.stop();
}
```


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
