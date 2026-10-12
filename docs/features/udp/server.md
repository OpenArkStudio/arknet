# Create a UDP Server

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the server before running the [matching client](client.md).

## Echo Server

The server replies through the session associated with the source endpoint.
Build this server and the [client](client.md) as separate executables with the `arknet::arknet` interface
target. Run the server first and the client in another terminal. Press Enter in
the server terminal to stop it.

```cpp
#include <arknet/udp/udp_server.hpp>
#include <iostream>
#include <string_view>

int main()
{
    arknet::udp_server server;
    server.bind_recv([](auto& session, std::string_view datagram)
    {
        session->async_send(datagram);
    });
    if (!server.start("127.0.0.1", 7000))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "UDP listening on 127.0.0.1:7000\n";
    std::cin.get();
    server.stop();
}
```


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
