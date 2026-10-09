#include <arknet/arknet.hpp>
#include <arknet/http/router.hpp>
#include "check.hpp"
#include <atomic>
#include <thread>

namespace
{
using verb = arknet::http::verb;
using status = arknet::http::status;
using field = arknet::http::field;
using request = arknet::http_route_request;
using response = arknet::http_route_response;

request incoming(verb method, std::string_view target)
{
    request value(method, target, 11);
    value.set(field::host, "localhost");
    return value;
}

template <class Server, class Client> void routed_loopback(Server& server, Client& client)
{
    auto router = std::make_shared<arknet::http_router>();
    router->add(verb::get, "/users/new", [](const auto&, auto& reply) { reply.body() = "static"; });
    router->add(verb::get, "/users/:id", [](const auto& context, auto& reply)
                { reply.body() = std::string(context.param("id")) + ':' + std::string(context.query); });
    arknet::bind_router(server, router);
    router.reset();
    messages received;
    client.set_auto_reconnect(false);
    client.bind_recv(
        [&](response& reply) {
            received.push(std::to_string(reply.result_int()) + ':' + reply.body() + ':' +
                          std::string(reply[field::allow]));
        });
    check(server.start("127.0.0.1", 0), "routed server starts");
    check(client.start("localhost", server.get_listen_port()), "routed client starts");
    client.async_send(incoming(verb::get, "/users/a%20b?q=one+two"));
    client.async_send(incoming(verb::head, "/users/new"));
    client.async_send(incoming(verb::get, "/users/new"));
    client.async_send(incoming(verb::post, "/users/new"));
    client.async_send(incoming(verb::options, "/users/new"));
    const auto values = received.wait(5);
    check(values[0] == "200:a b:q=one+two:", "owned route response and raw query");
    check(values[1] == "200::" && values[2] == "200:static:",
          "HEAD framing preserves the following pipelined response");
    check(values[3] == "405:Method Not Allowed:GET, HEAD, OPTIONS", "wire method rejection");
    check(values[4] == "204::GET, HEAD, OPTIONS", "wire automatic OPTIONS");
    client.stop();
    server.stop();
}
}

DOCTEST_TEST_CASE("HTTP routing matches static, named and wildcard paths with stable precedence")
{
    arknet::http_router router;
    router.add(verb::get, "/*rest",
               [](const auto& context, auto& reply) { reply.body() = "wild:" + std::string(context.param("rest")); });
    router.add(verb::get, "/users/:id",
               [](const auto& context, auto& reply) { reply.body() = "user:" + std::string(context.param("id")); });
    router.add(verb::get, "/users/new", [](const auto&, auto& reply) { reply.body() = "static"; });
    router.add(verb::post, "/users/:name",
               [](const auto& context, auto& reply) { reply.body() = "post:" + std::string(context.param("name")); });
    router.add(verb::get, "/files/*tail",
               [](const auto& context, auto& reply) { reply.body() = "file:" + std::string(context.param("tail")); });
    router.add(verb::get, "/files", [](const auto&, auto& reply) { reply.body() = "exact"; });
    router.add(verb::get, "/", [](const auto&, auto& reply) { reply.body() = "root"; });
    check(router(incoming(verb::get, "/users/new")).body() == "static",
          "static path wins independent of registration order");
    check(router(incoming(verb::get, "/users/42")).body() == "user:42", "one segment parameter");
    check(router(incoming(verb::post, "/users/42")).body() == "post:42",
          "parameter names belong to the selected method");
    check(router(incoming(verb::post, "/users/new")).result() == status::method_not_allowed,
          "static resource does not fall through to another parameter method");
    check(router(incoming(verb::get, "/files/a/b/")).body() == "file:a/b/",
          "wildcard retains separators and trailing slash");
    check(router(incoming(verb::get, "/files")).body() == "exact", "exact endpoint wins over an empty wildcard");
    check(router(incoming(verb::get, "/files/")).body() == "file:", "wildcard can capture an empty tail");
    check(router(incoming(verb::get, "/users/")).body() == "wild:users/", "parameter requires a nonempty segment");
    check(router(incoming(verb::get, "/")).body() == "root", "root path");
    check(router(incoming(verb::get, "/Users/new")).body() == "wild:Users/new", "path matching is case sensitive");
}

DOCTEST_TEST_CASE("HTTP routing validates targets and decodes path segments exactly once")
{
    arknet::http_router router;
    router.add(verb::get, "/:value",
               [](const auto& context, auto& reply)
               {
                   reply.body() = std::string(context.path) + ':' + std::string(context.param("value")) + ':' +
                                  std::string(context.query);
               });
    for (std::string_view target : {"",
                                    "http://localhost/x",
                                    "localhost:443",
                                    "*",
                                    "/%",
                                    "/%0",
                                    "/%GG",
                                    "/%00",
                                    "/%1f",
                                    "/%7f",
                                    "/a%2Fb",
                                    "/a%5cb",
                                    "/a\\b",
                                    "/a b",
                                    "/x#fragment",
                                    "/./x",
                                    "/x/..",
                                    "/%2e",
                                    "/%2E%2e",
                                    "/x?q=%",
                                    "/x?q=%0d",
                                    "/x?q=a b",
                                    "/x?q=#fragment"})
    {
        DOCTEST_INFO(target);
        check(router(incoming(verb::get, target)).result() == status::bad_request,
              "invalid request target rejects before route dispatch");
    }
    check(router(incoming(verb::get, "/a%20b?q=one+two%20three")).body() == "/a b:a b:q=one+two%20three",
          "path decoded and query left raw");
    check(router(incoming(verb::get, "/%252f")).body() == "/%2f:%2f:",
          "encoded percent does not trigger a second decode");
    check(router(incoming(verb::get, "/%23%3f")).body() == "/#?:#?:",
          "encoded delimiters remain within their path segment");
    check(router(incoming(verb::get, "/x?q=%2F%5C")).result() == status::ok,
          "encoded query data cannot change path boundaries");
    const auto head = router(incoming(verb::head, "/%00"));
    check(head.result() == status::bad_request && head.body().empty() && head[field::content_length] == "11",
          "HEAD error response has representation length but no body");
}

DOCTEST_TEST_CASE("HTTP routing rejects invalid configuration and duplicate resource methods")
{
    for (std::string_view pattern : {"", "relative", "*", "/x?q=1", "/x#y", "/:", "/:9id", "/:id/:id", "/x/*tail/end",
                                     "/x/*", "/%", "/%2f", "/../x"})
    {
        arknet::http_router router;
        bool rejected = false;
        try
        {
            router.add(verb::get, pattern, [](const auto&, auto&) {});
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        check(rejected, "invalid route pattern");
    }
    arknet::http_router router;
    router.add(verb::get, "/:first", [](const auto&, auto&) {});
    bool duplicate = false;
    try
    {
        router.add(verb::get, "/:second", [](const auto&, auto&) {});
    }
    catch (const std::invalid_argument&)
    {
        duplicate = true;
    }
    check(duplicate, "renaming a parameter does not produce a new resource");
    bool unknown = false;
    try
    {
        router.add(verb::unknown, "/x", [](const auto&, auto&) {});
    }
    catch (const std::invalid_argument&)
    {
        unknown = true;
    }
    check(unknown, "unknown HTTP method");
}

DOCTEST_TEST_CASE("HTTP routing provides method errors, HEAD fallback and OPTIONS semantics")
{
    arknet::http_router router;
    router.add(verb::get, "/value", [](const auto&, auto& reply) { reply.body() = "hello"; });
    router.add(verb::post, "/value", [](const auto&, auto& reply) { reply.body() = "posted"; });
    const auto missing = router(incoming(verb::get, "/missing"));
    check(missing.result() == status::not_found && missing.body() == "Not Found", "404 response");
    const auto wrong_method = router(incoming(verb::put, "/value"));
    check(wrong_method.result() == status::method_not_allowed &&
              wrong_method[field::allow] == "GET, HEAD, OPTIONS, POST",
          "405 contains all allowed resource methods");
    const auto head = router(incoming(verb::head, "/value"));
    check(head.result() == status::ok && head.body().empty() && head[field::content_length] == "5",
          "HEAD falls back to GET with representation length");
    const auto options = router(incoming(verb::options, "/value"));
    check(options.result() == status::no_content && options.body().empty() && !options.count(field::content_length) &&
              options[field::allow] == "GET, HEAD, OPTIONS, POST",
          "OPTIONS is bodyless and lists methods");
    check(router(incoming(verb::options, "/missing")).result() == status::not_found,
          "OPTIONS of a missing resource is 404");
    check(router(incoming(verb::options, "*"))[field::allow] == "GET, HEAD, OPTIONS, POST",
          "OPTIONS asterisk describes router capabilities");
    router.add(verb::head, "/value",
               [](const auto&, auto& reply)
               {
                   reply.content_length(42);
                   reply.body() = "ignored";
               });
    check(router(incoming(verb::head, "/value"))[field::content_length] == "42",
          "explicit HEAD handler wins and can set representation length");
    router.add(verb::options, "/value", [](const auto&, auto& reply) { reply.body() = "custom options"; });
    check(router(incoming(verb::options, "/value")).body() == "custom options", "explicit OPTIONS handler wins");
    auto closing = incoming(verb::get, "/value");
    closing.keep_alive(false);
    check(!router(closing).keep_alive(), "response inherits the request connection policy");
    router.add(verb::get, "/cached",
               [](const auto&, auto& reply)
               {
                   reply.result(status::not_modified);
                   reply.content_length(42);
                   reply.body() = "not transmitted";
               });
    const auto cached = router(incoming(verb::get, "/cached"));
    check(cached.body().empty() && cached[field::content_length] == "42",
          "304 can retain the representation length without a body");
}

DOCTEST_TEST_CASE("HTTP routing middleware runs in order and can short circuit any valid path")
{
    arknet::http_router router;
    std::string order;
    router.use(
        [&](const auto& context, auto& reply)
        {
            order += 'a';
            reply.set("X-Path", context.path);
            if (context.request[field::authorization].empty())
            {
                reply.result(status::unauthorized);
                return false;
            }
            return true;
        });
    router.use(
        [&](const auto&, auto&)
        {
            order += 'b';
            return true;
        });
    router.add(verb::get, "/:id",
               [&](const auto& context, auto& reply)
               {
                   order += 'c';
                   reply.body() = context.param("id");
               });
    check(router(incoming(verb::get, "/value")).result() == status::unauthorized && order == "a",
          "middleware can stop the route chain");
    order.clear();
    auto authorized = incoming(verb::get, "/value");
    authorized.set(field::authorization, "Bearer test");
    const auto routed = router(authorized);
    check(routed.body() == "value" && routed["X-Path"] == "/value" && order == "abc",
          "middleware precedes route handler");
    order.clear();
    check(router(incoming(verb::get, "/unmatched/path")).result() == status::unauthorized && order == "a",
          "middleware also guards automatic routing errors");
    order.clear();
    check(router(incoming(verb::get, "/%00")).result() == status::bad_request && order.empty(),
          "invalid targets never enter middleware");
}

DOCTEST_TEST_CASE("HTTP routing contains callback exceptions without exposing details")
{
    arknet::http_router router;
    unsigned failures = 0;
    router.bind_error(
        [&](std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const std::runtime_error& failure)
            {
                if (std::string_view(failure.what()) == "private detail")
                    ++failures;
            }
            throw std::logic_error("observer failure");
        });
    router.add(verb::get, "/fail",
               [](const auto&, auto& reply)
               {
                   reply.set("X-Private", "secret");
                   throw std::runtime_error("private detail");
               });
    const auto failed = router(incoming(verb::get, "/fail"));
    check(failed.result() == status::internal_server_error && failed.body() == "Internal Server Error" &&
              !failed.count("X-Private") && failures == 1,
          "fresh 500 response and contained error observer");
    router.add(verb::get, "/ok",
               [value = std::make_unique<std::string>("owned")](const auto&, auto& reply) { reply.body() = *value; });
    check(router(incoming(verb::get, "/ok")).body() == "owned",
          "move-only route handler and continuation after failure");
    router.use([](const auto&, auto&) -> bool { throw std::runtime_error("private detail"); });
    const auto failed_head = router(incoming(verb::head, "/ok"));
    check(failed_head.result() == status::internal_server_error && failed_head.body().empty() && failures == 2,
          "middleware exception and HEAD framing");
}

DOCTEST_TEST_CASE("HTTP route responses own their data and support concurrent dispatch after configuration")
{
    arknet::http_router router;
    std::atomic<unsigned> calls{};
    router.add(verb::get, "/:id",
               [&](const auto& context, auto& reply)
               {
                   ++calls;
                   reply.body() = context.param("id");
               });
    auto value = incoming(verb::get, "/original");
    auto owned = router(value);
    value.target("/modified");
    check(owned.body() == "original", "response remains valid after the request changes");
    std::atomic<unsigned> invalid{};
    std::vector<std::jthread> threads;
    for (unsigned worker = 0; worker < 4; ++worker)
        threads.emplace_back(
            [&]
            {
                for (unsigned iteration = 0; iteration < 100; ++iteration)
                    if (router(incoming(verb::get, "/parallel")).body() != "parallel")
                        ++invalid;
            });
    threads.clear();
    check(calls == 401 && invalid == 0, "immutable routing tables can dispatch concurrently");
}

DOCTEST_TEST_CASE("HTTP router adapters own their router and preserve pipelined response framing")
{
    arknet::http_server server;
    arknet::http_client client;
    routed_loopback(server, client);
}

#if defined(ARKNET_ENABLE_SSL)
DOCTEST_TEST_CASE("HTTPS uses the same router and response ownership rules")
{
    const std::string certificates = ARKNET_TEST_CERT_DIR;
    arknet::https_server server;
    arknet::https_client client;
    server.set_cert_file("", certificates + "/server.pem", certificates + "/server-key.pem", "");
    client.load_verify_file(certificates + "/ca.pem");
    routed_loopback(server, client);
}
#endif
