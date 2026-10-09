# TLS Usage

Enable TLS for either provider and use the `arknet::arknet` interface target. CMake finds and links
OpenSSL 1.1.1 or newer. See the [build guide](../guide.md) for installation and
provider configuration; every translation unit must use consistent settings.

```sh
cmake -S . -B build-tls -DARKNET_ENABLE_SSL=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
```

## Secure TCP Echo Server

Build each block as a separate C++20 executable using the `arknet::arknet`
interface target with TLS enabled. The examples use the repository's local
test CA and server identity. Run them from the repository root, or pass a
test-certificate directory as the first argument of each program. These public
test keys are not deployment credentials. The test certificate covers
`localhost` and `127.0.0.1`. Start the server first, then run the client in
another terminal. Press Enter in the server terminal to stop it.

```cpp
#include <arknet/tcp/tcps_server.hpp>
#include <iostream>
#include <string>
#include <string_view>

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

## Require a Client Certificate

For mTLS, add the following before the example's server start call:

```cpp
arknet::error_code server_error;
server.load_verify_file(certs + "/ca.pem", server_error);
if (server_error)
    return 1;
server.set_verify_mode(asio::ssl::verify_peer |
    asio::ssl::verify_fail_if_no_peer_cert, server_error);
if (server_error)
    return 1;
```

After constructing the client and before its start call, supply its identity:

```cpp
client.set_cert_file(certs + "/ca.pem", certs + "/client.pem",
    certs + "/client-key.pem", "");
if (arknet::get_last_error())
    return 1;
```

Keep client server-identity verification enabled. Without a trusted client
certificate, the server rejects the connection. A TLS 1.3 client may briefly
report start success before receiving that rejection; handle disconnect events
and application readiness instead of relying on start alone for mTLS admission.
Map verified certificate identity to application permissions separately.

## Apply the Same Policy to WSS and HTTPS

For WSS, use `wss_client`/`wss_server` from
`<arknet/http/wss_client.hpp>` and `<arknet/http/wss_server.hpp>`. Configure
identity and CA trust in the same way. Start the server without `use_dgram` and
start the client with `(host, port, "/echo")`. WebSocket supplies message
boundaries and performs its HTTP upgrade after TLS; see
[WebSocket usage](../websocket/usage.md) for frame types and upgrade headers.

For HTTPS, use `https_client`/`https_server` and the corresponding headers.
Certificate and mTLS policy remain the same, while send/receive data are HTTP
request/response objects rather than byte views. See [HTTP usage](../http/usage.md).
HTTPS does not add WebSocket upgrade routing or change application authorization.

All secure endpoint families expose `bind_handshake` and `ssl_stream()`.
For handshake diagnosis, register a client callback before start:

```cpp
client.bind_handshake([&]
{
    const auto error = arknet::get_last_error();
    if (error)
        std::cerr << "TLS handshake: " << error.message() << '\n';
});
```

An untrusted CA and a DNS/IP identity mismatch are different failures. Check
the host passed to `start`, the certificate's subject alternative names, trust
configuration and validity period. Connecting by numeric IP requires an IP
identity in the certificate; setting a DNS SNI name is not a substitute.

## Shutdown and Extension

Secure sends retain normal payload ownership, completion signatures and bounded
queues. TLS write completion is not remote application acknowledgment. Configure
`set_connect_timeout` and `set_disconnect_timeout` before start; shutdown attempts
TLS close-notify within the disconnect bound. WSS closes its WebSocket protocol
first, then TLS and TCP.

Use `tcps_client_t`, `tcps_session_t` and `tcps_server_t` for CRTP extensions;
WSS and HTTPS have their matching `_t` types. Native SSL context options are
available through inheritance. Configure them before starting, and serialize
stream access with the endpoint's IO lane. Reconnect creates a fresh TLS stream
and performs verification again; do not assume an old native stream reference
remains valid.

Request stop from an IO callback and wait on the owner thread. Any external
context must keep running until stop completes; destroy endpoints before
stopping the host runners. See [IO models](../threading.md),
[security regressions](https://github.com/OpenArkStudio/arknet/blob/main/tests/tls.cpp)
and [performance](performance.md).
