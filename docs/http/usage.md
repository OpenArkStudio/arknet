# Using HTTP and HTTPS

[Introduction](../http.md) | [Routing](routing.md) | [Performance](performance.md)

## HTTP Echo Server

Use the `arknet::arknet` interface target in a C++20 application following the
[build guide](../guide.md). Build the server and client blocks as separate
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

## Messages, Callbacks and Ownership

Clients receive `response<string_body>&`; servers receive a session reference
and `request<string_body>&`. These references are valid only during the
callback. Copy or move a message before retaining it. Accepted sends own their
serialized message until completion; later changes to the caller's object do
not change the queued message.

After changing a body, call `prepare_payload()`, or deliberately configure
chunked framing. Write completion counts HTTP wire bytes, including headers,
and does not mean the peer has received or processed the request. Immediate
rejection and accepted completion use the [common send contract](../runtime.md).

## Keep-Alive and Pipelining

Several requests may be queued on one connection. Clients match responses to
request order and skip information responses before reporting the final one.
The server application must send responses in request order. A router's
synchronous dispatch naturally preserves that order; work moved to other
threads must be re-ordered before sending.

HEAD has no response body; an `empty_body` response can advertise the
representation's Content-Length. A response requiring EOF closes the
connection after its write; later queued sends finish with cancellation.
The server sends `100 Continue` before waiting for an expected body.

## Limits and Errors

Configure `set_http_header_limit(bytes)` and `set_http_body_limit(bytes)`
before starting. Defaults are 8192 bytes and 16 MiB. Changing a limit during a
read applies to the next parser. Parser/framing errors and oversized messages
close the connection. Application status codes such as 404 are valid HTTP
responses; inspect `response.result()`.

Use `bind_disconnect` and the callback's thread-local `get_last_error()`
to inspect connection errors. Disable automatic reconnect for a one-shot
exchange. Reconnection does not replay application requests.

## HTTPS Server and Client

With `ARKNET_ENABLE_SSL=ON`, the two complete HTTP programs above also serve as
HTTPS examples after the following changes. The request/response callbacks,
echo validation and shutdown code stay the same. The public test certificate
and key below are for local testing; deployed services need their own identity
and CA trust. See [TLS examples](../tls/usage.md) for certificate paths and mTLS.

In the server program, replace its header with
`<arknet/http/https_server.hpp>` and replace the server declaration with:

```cpp
arknet::https_server server;
server.set_cert_file("", "tests/certs/server.pem", "tests/certs/server-key.pem", "");
if (arknet::get_last_error())
{
    std::cerr << arknet::get_last_error().message() << '\n';
    return 1;
}
```

Listen on `server.start("127.0.0.1", 8443)` and change the printed URL to
`https://localhost:8443/echo`.

In the client program, replace its header with
`<arknet/http/https_client.hpp>` and replace the client declaration with:

```cpp
arknet::https_client client;
arknet::error_code error;
client.load_verify_file("tests/certs/ca.pem", error);
if (error)
{
    std::cerr << error.message() << '\n';
    return 1;
}
```

Connect with `client.start("localhost", 8443)` and set the request's `Host`
field to `"localhost:8443"`. The connection host controls DNS/IP verification;
the HTTP Host header does not change the TLS identity. Clients verify the
certificate chain and host identity by default. Run the programs from the
repository root so the test-certificate paths resolve.

## Custom Sessions

Custom sessions derive from `http_session_t<MySession>` and are supplied to
`http_server_t<MySession>`; HTTPS provides the corresponding `https_*` forms.
The [HTTP tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/http.cpp)
cover raw-peer chunked messages, HEAD, limits, Expect and pipelining.

## Routing and Business Work

Use [`http_router`](routing.md) for method/path dispatch and middleware.
Route callbacks execute synchronously on the receiving lane. Long CPU or
blocking work needs an application worker pool, bounded admission and ordered
responses; see [IO models](../threading.md).
