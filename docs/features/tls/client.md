# Create a TLS TCP Client

Configure a C++20 application using [Installation](../../get-started/installation.md). Start the [matching server](server.md), then run this client in another terminal.

Enable `ARKNET_ENABLE_SSL=ON` and configure OpenSSL. This example loads the CA from `tests/certs/ca.pem`; run from the repository root or pass the certificate directory as the first argument. See [Configuration](configuration.md) and the [server example](server.md) for server certificates and mTLS. The public test certificates and keys are only for local testing.

## Secure TCP Echo Client

Load the CA before connecting. The client verifies the certificate chain and
the `localhost` identity passed to `start`; DNS connections also send SNI.
Do not disable peer verification to make a certificate mismatch succeed.

```cpp
#include <arknet/tcp/tcps_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv)
{
    const std::string certs = argc > 1 ? argv[1] : "tests/certs";
    std::promise<std::string> reply;
    auto received = reply.get_future();
    bool received_once = false;

    arknet::tcps_client client;
    client.set_auto_reconnect(false);
    arknet::error_code error;
    client.load_verify_file(certs + "/ca.pem", error);
    if (error)
    {
        std::cerr << error.message() << '\n';
        return 1;
    }
    client.bind_recv([&](std::string_view bytes)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(std::string(bytes));
        }
    });
    if (!client.start("localhost", 7443, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("hello TLS")))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    const bool matched = ready && received.get() == "hello TLS";
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

For deployed services, load the service's certificate chain and matching PEM
private key. The final `set_cert_file` argument is the private-key password;
the first optionally loads CA trust. `set_cert_buffer` accepts the same PEM
material from memory. Check `get_last_error()` immediately after these helpers.
Inherited Asio methods support throwing and `error_code` overloads; the example
uses the explicit error overload for CA loading.

Client `async_start` begins connection/handshake without waiting. Its return
reports initiation; use `bind_handshake` and `bind_connect` to observe the final
TLS/connection result on the IO lane. WSS additionally reports HTTP upgrade.


[Configuration and advanced usage](configuration.md) · [Performance](performance.md)
