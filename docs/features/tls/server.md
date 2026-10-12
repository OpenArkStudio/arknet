# Create a TLS TCP Server

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the server before running the [matching client](client.md).

## Secure TCP Echo Server

Build each block as a separate C++20 executable using the `arknet::arknet`
interface target with TLS enabled. The examples use the repository's local
test CA and server identity. Run them from the repository root, or pass a
test-certificate directory as the first argument of each program. These public
test keys are not deployment credentials. The test certificate covers
`localhost` and `127.0.0.1`. Start the server first, then run the client in
another terminal. Press Enter in the server terminal to stop it.

```cpp
#include <iostream>
#include <string>
#include <string_view>

#include <arknet/tcp/tcps_server.hpp>

int main(int argc, char** argv)
{
    const std::string certs = argc > 1 ? argv[1] : "tests/certs";
    arknet::tcps_server server;
    server.set_cert_file("", certs + "/server.pem", certs + "/server-key.pem", "");
    if (arknet::get_last_error())
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    server.bind_recv([](auto& session, std::string_view bytes)
    {
        session->async_send(bytes);
    });
    if (!server.start("127.0.0.1", 7443, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "TLS TCP listening on 127.0.0.1:7443\n";
    std::cin.get();
    server.stop();
}
```


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
