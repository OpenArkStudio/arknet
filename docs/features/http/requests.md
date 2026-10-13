# Send a Single HTTP Request

`arknet::http_request` runs one plain HTTP/1.0 or HTTP/1.1 transaction on an external `io_context`. Use it for a request with a bounded lifetime; use `http_client` for persistent connections and pipelining.

## Request and Response

```cpp
#include <iostream>
#include <utility>

#include <arknet/http/request.hpp>

int main()
{
    asio::io_context context;
    arknet::http_request operation(context);
    arknet::http_request::request_type request(
        arknet::http::verb::post, "/echo", 11);
    request.set(arknet::http::field::content_type, "text/plain");
    request.body() = "hello HTTP";
    auto reply = operation.async_request(
        "127.0.0.1", "8080", std::move(request), asio::use_future);
    context.run();
    auto [error, response] = reply.get();
    if (error)
    {
        std::cerr << error.message() << '\n';
        return 1;
    }
    std::cout << response.result_int() << ' ' << response.body() << '\n';
    return response.result() == arknet::http::status::ok ? 0 : 1;
}
```

The request is owned by the operation. A missing `Host` field is populated from the host and numeric port; payload framing is prepared automatically. Requests use `Connection: close`. Targets must already be percent encoded.

Omitting the port selects **80**, with either a callback or future. An optional
`http_request_options` argument goes before the completion parameter. The host is
a domain name or an unbracketed IP literal; the path belongs in the request target:

```cpp
auto reply = operation.async_request("example.com", std::move(request), asio::use_future);
```

Explicit ports keep their existing syntax. The generated `Host` omits the default
port, appends a non-default numeric port and brackets IPv6 literals. This interface
handles plain HTTP only; use `https_client` for HTTPS.

Cookies and other fields use the normal Beast request API:

```cpp
arknet::http_request::request_type request(
    arknet::http::verb::get, "/items?name=a%20b", 11);
request.set(arknet::http::field::cookie, "session=abc; theme=dark");
```

GET, POST, PUT, DELETE and other HTTP methods share the same interface. A nonzero error indicates a transport, timeout, cancellation, validation or parsing failure. A complete `404` or `500` response is returned with no transport error; the caller decides which status codes succeed.

## Deadlines and Limits

Pass `http_request_options` immediately before the callback or `asio::use_future` token.

| Option | Default | Scope |
| --- | --- | --- |
| `connect_timeout` | 5 seconds | DNS resolution and TCP connection combined |
| `io_timeout` | 20 seconds | Request write and complete response combined |
| `header_limit` | 8192 bytes | Response headers |
| `body_limit` | 8 MiB | Response body |

Timeouts must be positive; `header_limit` must be nonzero. Timeouts return `asio::error::timed_out`. Receive limits are independent of operating system socket buffers. They do not limit the outgoing body.

## Callbacks and Cancellation

```cpp
operation.async_request("127.0.0.1", "8080", std::move(request),
    [](arknet::error_code error, arknet::http_request::response_type response)
    {
        // Inspect error and response.result() here.
    });
// operation.cancel();
```

Completion runs once on the operation's strand, including when multiple threads run the context. Callbacks can own move-only state. The future overload returns `std::pair<error_code, response_type>` and reports errors in the pair.

`cancel()` and `stop()` are nonblocking and idempotent. Destroying the object cancels unfinished work. Canceled operations return `asio::error::operation_aborted`; late socket and timer callbacks do not complete the request again. A second request on the same object returns `asio::error::already_started`. Create a new object for another transaction.

Keep the context alive and running until completion. Explicitly stopping the context prevents queued completion callbacks from running. This one-shot interface covers plain HTTP; use `https_client` for HTTPS. Protocol upgrades such as `101 Switching Protocols` are rejected; use `ws_client` for WebSocket.

## Tests

`tests/httprequest.cpp` covers request ownership, arbitrary methods and status codes, binary bodies, informational responses, chunked and close-delimited bodies, HEAD, malformed framing, limits, separate deadlines, cancellation and multiple I/O threads.
