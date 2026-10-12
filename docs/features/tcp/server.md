# Create a TCP Server

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the server before running the [matching client](client.md).

## Echo Server

Build the server and client blocks as separate C++20 executables using the
`arknet::arknet` interface target. Run the server first, then run the client in
another terminal. Press Enter in the server terminal to stop it. Both endpoints
use `use_dgram` length framing; the client waits on its main thread, not in an IO
callback.

```cpp
#include <arknet/tcp/tcp_server.hpp>
#include <iostream>
#include <string_view>

int main()
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view bytes)
    {
        session->async_send(bytes);
    });
    if (!server.start("127.0.0.1", 7000, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "TCP listening on 127.0.0.1:7000\n";
    std::cin.get();
    server.stop();
}
```


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
