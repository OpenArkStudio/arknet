# HTTP Routing

[HTTP and HTTPS](../http.md)

`arknet::http_router` is a header-only router for the existing HTTP/1 and HTTPS
endpoints. It dispatches a complete request synchronously and returns an owned
`http::response<http::string_body>`. No additional dependency is required.

## Register and Bind

```cpp
#include <arknet/arknet.hpp>
#include <arknet/http/router.hpp>

auto router = std::make_shared<arknet::http_router>();
router->add(arknet::http::verb::get, "/users/:id",
    [](const arknet::http_route_context& context,
       arknet::http_route_response& response)
    {
        response.set(arknet::http::field::content_type, "text/plain");
        response.body() = std::string(context.param("id"));
    });

arknet::http_server server;
arknet::bind_router(server, router);
server.start("127.0.0.1", 8080);
```

`bind_router` captures a `shared_ptr<const http_router>` and keeps the router
alive while the server uses its receive handler. It also works with
`https_server` and custom HTTP/HTTPS sessions. Configure certificates and peer
verification through the existing TLS API. Binding replaces the server's
receive handler. Alternatively, call `router(request)` inside an application
`bind_recv` callback and pass the returned response to `session->async_send`.

Configure routes, middleware and error observers before handling requests.
Configuration changes, including moving the router, must not overlap dispatch.
Concurrent dispatch after configuration is supported; handlers and middleware
must synchronize any mutable application state they share. Move-only callbacks
are supported. The router itself is movable and not copyable.

## Paths and Context

| Pattern | Matching rule | Example |
| --- | --- | --- |
| `/users/new` | Literal path | `/users/new` |
| `/users/:id` | One nonempty segment | `/users/42` gives `id = "42"` |
| `/files/*tail` | Remaining segments, including an empty tail | `/files/a/b` gives `tail = "a/b"` |

Parameter names start with a letter or underscore and contain only ASCII
letters, digits or underscores. Names must be unique within a pattern. A
wildcard is allowed only as the final segment. Invalid patterns and duplicate
method/pattern registrations throw `std::invalid_argument`; changing a parameter
name does not create a different pattern for this check.

Literal segments take precedence over parameter segments, then wildcards,
compared from left to right. An exact path wins over an empty wildcard. First
the most specific resource is selected, then its method is selected. For
example, with `GET /users/new` and `POST /users/:id`, `POST /users/new` returns
405 instead of falling through to the parameter route. Registration order
does not change this behavior. Paths are case sensitive; repeated slashes and
trailing slashes are preserved.

`http_route_context` exposes:

| Member | Meaning |
| --- | --- |
| `request` | A const reference to the complete Beast request |
| `path` | The decoded path used for routing |
| `query` | The original query text, without `?`; it is not form-decoded |
| `parameters` | Selected route's parameter names and values |
| `param(name)` | Parameter value, or an empty view if absent |

The context and its views are valid only during the synchronous callback.
Copy values that must outlive it. Responses own their headers and body and can
be moved into `async_send`. The router sets the HTTP version, initially inherits
the request's connection policy, and prepares response framing. A handler may
set `keep_alive(false)` to close the connection after sending.

Request targets must be origin-form paths beginning with `/`, or exactly `*`
for OPTIONS. Absolute-form proxy targets and CONNECT authority targets return
400. Paths are percent-decoded once. Queries retain their original bytes and
`+` characters. Invalid escapes, raw non-URI characters, controls, fragments,
backslashes, encoded path slashes/backslashes and decoded `.`/`..` segments
return 400 before middleware runs. Percent-encoded non-ASCII bytes are retained
without Unicode normalization or UTF-8 validation. This is routing validation,
not a filesystem sandbox or a form/query parser.

## Methods and Middleware

Missing resources return 404. A method not registered for the selected resource
returns 405 with an `Allow` header. HEAD uses an explicit HEAD handler if present,
otherwise the GET handler. The response has no body and preserves an explicit
`Content-Length` or computes the GET representation length. This also applies
to HEAD error responses.

OPTIONS uses its explicit handler if present. Otherwise a known resource
returns 204 with `Allow`; a missing resource returns 404. `OPTIONS *` returns
204 and lists the router's registered methods. Automatic lists include OPTIONS
and include HEAD when GET is registered. Bodyless statuses such as 204 and 304
are framed without a response body.

```cpp
router->use([](const arknet::http_route_context& context,
               arknet::http_route_response& response)
{
    if (context.request[arknet::http::field::authorization].empty())
    {
        response.result(arknet::http::status::unauthorized);
        return false;
    }
    return true;
});
```

Middleware runs in registration order before the handler or automatic
404/405/OPTIONS response. `true` continues; `false` sends the middleware's
current response and skips the remaining chain. Matched route parameters are
available for selected handlers, including HEAD fallback. Middleware runs on
the session's IO executor, as do route handlers. Long computation blocks that
executor; this synchronous API does not provide asynchronous continuation or
streaming.

Exceptions from handlers and middleware produce a fresh 500 response with
`Internal Server Error`, without partial headers or exception details. An
optional `bind_error` observer receives `std::exception_ptr` on the same
executor; exceptions thrown by that observer are contained.

```cpp
router->bind_error([](std::exception_ptr error)
{
    try { std::rethrow_exception(error); }
    catch (const std::exception& failure) { /* Record failure.what(). */ }
});
```

The permanent [router tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/router.cpp)
cover validation, matching, middleware, errors, concurrency and HTTP/HTTPS
pipelining. Build and run `arknet_router_test`, or use the project's
`arknet_check` target.
