# Create a WebSocket Server

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the server before running the [matching client](client.md).

## Binary Echo Server

WebSocket has its own message framing; do not pass `use_dgram` or a TCP delimiter.
The server preserves the received message's text/binary type when echoing it.
Build this server and the [client](client.md) as separate executables using the `arknet::arknet` interface
target. Run the server first, then the client in another terminal. Press Enter in
the server terminal to stop it.

```cpp
#include <iostream>
#include <string_view>

#include <arknet/websocket/ws_server.hpp>

int main()
{
    arknet::ws_server server;
    server.bind_recv([](auto& session, std::string_view bytes)
    {
        session->ws_stream().binary(session->ws_stream().got_binary());
        session->async_send(bytes);
    });
    if (!server.start("127.0.0.1", 7001))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "WebSocket listening on ws://127.0.0.1:7001/echo\n";
    std::cin.get();
    server.stop();
}
```


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
