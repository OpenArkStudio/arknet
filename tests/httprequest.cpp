#include <atomic>
#include <thread>
#include <vector>

#include <arknet/http/http_server.hpp>
#include <arknet/http/request.hpp>
#include <arknet/tcp/tcp_server.hpp>
#include "check.hpp"

namespace {
using request_type = arknet::http_request::request_type;
using response_type = arknet::http_request::response_type;

class io_runner
{
public:
    asio::io_context context;
    explicit io_runner(std::size_t count = 1)
        : guard_(asio::make_work_guard(context)) {
        for (std::size_t i = 0; i < count; ++i)
        {
            workers_.emplace_back([this] { context.run(); });
        }
    }
    ~io_runner() {
        guard_.reset();
        context.stop();
        for (auto& worker : workers_)
        {
            worker.join();
        }
    }

private:
    asio::executor_work_guard<asio::io_context::executor_type> guard_;
    std::vector<std::thread> workers_;
};

request_type message(arknet::http::verb method = arknet::http::verb::get, std::string body = {}) {
    request_type request(method, "/?encoded=a%20b", 11);
    request.body() = std::move(body);
    return request;
}

class wire_server
{
public:
    explicit wire_server(std::string response, bool close = false) {
        server_.bind_recv([response = std::move(response), close](auto& session, std::string_view) {
            if (session->template get_user_data<bool>())
            {
                return;
            }
            session->set_user_data(true);
            session->async_send(response, [session, close](const arknet::error_code&, std::size_t) {
                if (close)
                {
                    session->stop();
                }
            });
        });
        check(server_.start("127.0.0.1", 0), "wire peer starts");
    }
    ~wire_server() {
        server_.stop();
    }
    std::string service() const {
        return std::to_string(server_.get_listen_port());
    }

private:
    arknet::tcp_server server_{1024, 16 * 1024 * 1024, 2};
};

arknet::http_request::result_type fetch(std::string response, bool close = false,
    arknet::http_request_options options = {}, arknet::http::verb method = arknet::http::verb::get) {
    wire_server peer(std::move(response), close);
    io_runner runtime;
    arknet::http_request operation(runtime.context);
    auto future = operation.async_request("127.0.0.1", peer.service(), message(method), options, asio::use_future);
    return await(future);
}
} // namespace

DOCTEST_TEST_CASE("HTTP single requests own request data and expose arbitrary status and verb") {
    arknet::http_server server(1024, 16 * 1024 * 1024, 2);
    messages received;
    server.bind_recv([&](auto& session, request_type& request) {
        received.push(std::string(request.method_string()) + ":" + std::string(request.target()) + ":" +
                      std::string(request[arknet::http::field::cookie]) + ":" + request.body());
        response_type response(arknet::http::status::not_found, 11);
        response.body() = std::string("binary\0body", 11);
        response.keep_alive(false);
        response.prepare_payload();
        session->async_send(std::move(response));
    });
    check(server.start("127.0.0.1", 0), "HTTP request server starts");
    io_runner runtime(4);
    arknet::http_request operation(runtime.context);
    auto request = message(arknet::http::verb::put, "owned");
    request.set(arknet::http::field::cookie, "a=1; b=2");
    auto future =
        operation.async_request("127.0.0.1", std::to_string(server.get_listen_port()), request, asio::use_future);
    request.body() = "changed";
    const auto [ec, response] = await(future);
    check(!ec, "completed non-200 response is a successful HTTP exchange");
    check(response.result() == arknet::http::status::not_found, "status exposed to caller");
    check(response.body() == std::string("binary\0body", 11), "binary body preserved");
    check(received.wait(1).front() == "PUT:/?encoded=a%20b:a=1; b=2:owned", "request retained all fields");
    auto repeat = operation.async_request("127.0.0.1", "80", message(), asio::use_future);
    check(await(repeat).first == asio::error::already_started, "single-use object rejects repeated requests");
    server.stop();
}

DOCTEST_TEST_CASE("HTTP host-only single requests preserve completion options and validation") {
    io_runner runtime;
    arknet::http_request future_operation(runtime.context);
    auto invalid_host = future_operation.async_request("invalid/host", message(), asio::use_future);
    check(await(invalid_host).first == asio::error::invalid_argument, "host-only future validates host");

    arknet::http_request options_operation(runtime.context);
    arknet::http_request_options options;
    options.header_limit = 0;
    auto invalid_options = options_operation.async_request("localhost", message(), options, asio::use_future);
    check(await(invalid_options).first == asio::error::invalid_argument, "host-only future preserves options");

    for (bool explicit_options : {false, true})
    {
        arknet::http_request callback_operation(runtime.context);
        callback_operation.cancel();
        std::promise<arknet::error_code> completed;
        auto future = completed.get_future();
        auto callback = [&, owned = std::make_unique<int>(42)](arknet::error_code ec, response_type) {
            check(*owned == 42, "host-only callback retains move-only captures");
            completed.set_value(ec);
        };
        if (explicit_options)
        {
            callback_operation.async_request(
                "localhost", message(), arknet::http_request_options{}, std::move(callback));
        }
        else
        {
            callback_operation.async_request("localhost", message(), std::move(callback));
        }
        check(await(future) == asio::error::operation_aborted, "host-only callback preserves cancellation");
    }
}

DOCTEST_TEST_CASE("HTTP single requests parse informational chunked close-delimited and HEAD responses") {
    auto [ec, response] = fetch("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                                "2;ext=yes\r\nhe\r\n3\r\nllo\r\n0\r\nX-Trailer: ok\r\n\r\n");
    check(!ec && response.body() == "hello", "final chunked response completes without waiting for EOF");
    auto eof = fetch("HTTP/1.0 200 OK\r\n\r\nclose-delimited", true);
    check(!eof.first && eof.second.body() == "close-delimited", "close-delimited response completes at EOF");
    auto head = fetch("HTTP/1.1 200 OK\r\nContent-Length: 1234\r\n\r\n", false, {}, arknet::http::verb::head);
    check(!head.first && head.second.body().empty(), "HEAD doesn't wait for the advertised body");
    auto upgrade = fetch("HTTP/1.1 101 Switching Protocols\r\nConnection: upgrade\r\nUpgrade: websocket\r\n\r\n");
    check(upgrade.first == asio::error::operation_not_supported, "protocol upgrades are explicit errors");
}

DOCTEST_TEST_CASE("HTTP single requests reject malformed framing and enforce response limits") {
    for (const auto& response : {"HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n",
             "HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
             "INVALID 200 OK\r\nContent-Length: 1\r\n\r\nx", "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhi"})
    {
        check(bool(fetch(response, true).first), response);
    }
    arknet::http_request_options limits;
    limits.header_limit = 128;
    limits.body_limit = 32;
    check(bool(fetch("HTTP/1.1 200 OK\r\nX-Large: " + std::string(129, 'x') + "\r\n\r\n", false, limits).first),
        "header limit enforced");
    check(bool(fetch("HTTP/1.1 200 OK\r\nContent-Length: 33\r\n\r\n" + std::string(33, 'x'), false, limits).first),
        "body limit enforced");
    auto boundary = fetch("HTTP/1.1 200 OK\r\nContent-Length: 32\r\n\r\n" + std::string(32, 'x'), false, limits);
    check(!boundary.first && boundary.second.body().size() == 32, "body limit boundary succeeds");
}

DOCTEST_TEST_CASE("HTTP single request IO deadlines finish once and ignore late operations") {
    wire_server peer("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhi");
    io_runner runtime(4);
    arknet::http_request operation(runtime.context);
    arknet::http_request_options options;
    options.connect_timeout = 1s;
    options.io_timeout = 50ms;
    std::promise<arknet::error_code> completion;
    auto future = completion.get_future();
    std::atomic<int> calls{};
    const auto start = std::chrono::steady_clock::now();
    operation.async_request("127.0.0.1", peer.service(), message(), options,
        [&, owned = std::make_unique<int>(42)](arknet::error_code ec, response_type) {
            if (++calls == 1)
            {
                completion.set_value(ec);
            }
        });
    check(await(future) == asio::error::timed_out, "stalled body reports timeout");
    check(std::chrono::steady_clock::now() - start < 1s, "IO deadline does not use the connect deadline");
    operation.cancel();
    operation.stop();
    std::this_thread::sleep_for(100ms);
    check(calls == 1, "timeout and late cancellation complete once");
}

DOCTEST_TEST_CASE("HTTP single request connect deadline covers DNS and "
                  "releases callback captures") {
    io_runner runtime(4);
    arknet::http_request_options options;
    options.connect_timeout = 1ns;
    options.io_timeout = 5s;
    for (const auto* host : {"127.0.0.1", "localhost"})
    {
        for (int repeat = 0; repeat < 32; ++repeat)
        {
            arknet::http_request operation(runtime.context);
            std::promise<arknet::error_code> completion;
            auto future = completion.get_future();
            auto captured = std::make_shared<int>(7);
            std::weak_ptr<int> released = captured;
            operation.async_request(host, "1", message(), options,
                [&, captured = std::move(captured)](
                    arknet::error_code ec, response_type) { completion.set_value(ec); });
            const auto ec = await(future);
            DOCTEST_INFO("host=", host, ", repeat=", repeat, ", error=", ec.message());
            check(ec == asio::error::timed_out, "connect deadline applies before TCP connection");
            const auto limit = std::chrono::steady_clock::now() + 1s;
            while (!released.expired() && std::chrono::steady_clock::now() < limit)
            {
                std::this_thread::yield();
            }
            check(released.expired(), "completion releases callback captures while "
                                      "request handle remains alive");
        }
    }
}

DOCTEST_TEST_CASE("HTTP single request deadline wins over queued response "
                  "success and errors") {
    for (bool truncated : {false, true})
    {
        arknet::tcp_server server;
        std::promise<std::shared_ptr<arknet::tcp_session>> connected;
        auto peer = connected.get_future();
        server.bind_recv([&](auto& session, std::string_view) {
            if (session->template get_user_data<bool>())
            {
                return;
            }
            session->set_user_data(true);
            connected.set_value(session);
        });
        check(server.start("127.0.0.1", 0), "deadline peer starts");
        asio::io_context context;
        arknet::http_request operation(context);
        arknet::http_request_options options;
        options.connect_timeout = 5s;
        options.io_timeout = 250ms;
        std::atomic<int> calls{};
        std::promise<arknet::error_code> completion;
        auto result = completion.get_future();
        operation.async_request("127.0.0.1", std::to_string(server.get_listen_port()), message(), options,
            [&](arknet::error_code ec, response_type) {
                if (++calls == 1)
                {
                    completion.set_value(ec);
                }
            });
        const auto setup_limit = std::chrono::steady_clock::now() + 2s;
        while (peer.wait_for(0ms) != std::future_status::ready && std::chrono::steady_clock::now() < setup_limit)
        {
            context.poll_one();
            std::this_thread::yield();
        }
        check(peer.wait_for(0ms) == std::future_status::ready, "request reaches the peer before pausing IO");
        auto session = peer.get();
        std::promise<void> sent;
        auto response_ready = sent.get_future();
        session->async_send(truncated ? "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhi"
                                      : "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello",
            [&, session, truncated](arknet::error_code ec, std::size_t) {
                check(!ec, "peer response sends while request IO is paused");
                if (truncated)
                {
                    session->stop();
                }
                sent.set_value();
            });
        await(response_ready);
        std::this_thread::sleep_for(300ms);
        context.run();
        check(await(result) == asio::error::timed_out, "expired deadline applies to queued success and errors");
        check(calls == 1, "timer and IO completion finish the request once");
        server.stop();
    }
}

DOCTEST_TEST_CASE("HTTP single requests cancel before start during response and on destruction") {
    io_runner runtime(4);
    arknet::http_request canceled(runtime.context);
    canceled.cancel();
    auto before = canceled.async_request("127.0.0.1", "1", message(), asio::use_future);
    check(await(before).first == asio::error::operation_aborted, "pre-start cancellation applies to pending request");
    wire_server peer("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhi");
    auto operation = std::make_unique<arknet::http_request>(runtime.context);
    auto response = operation->async_request("127.0.0.1", peer.service(), message(), asio::use_future);
    operation.reset();
    check(await(response).first == asio::error::operation_aborted, "destruction cancels and completes future");
    arknet::http_request active(runtime.context);
    auto future = active.async_request("127.0.0.1", peer.service(), message(), asio::use_future);
    std::this_thread::sleep_for(20ms);
    active.cancel();
    check(await(future).first == asio::error::operation_aborted, "incomplete response is cancelable");
}

DOCTEST_TEST_CASE("HTTP single requests reject a TRACE body without throwing from IO") {
    asio::io_context context;
    arknet::http_request operation(context);
    auto future =
        operation.async_request("127.0.0.1", "1", message(arknet::http::verb::trace, "body"), asio::use_future);
    DOCTEST_CHECK_NOTHROW(context.run());
    if (future.wait_for(0ms) != std::future_status::ready)
    {
        operation.cancel();
        context.run();
    }
    check(await(future).first == asio::error::invalid_argument, "TRACE payload validation completes with an error");
}

DOCTEST_TEST_CASE("HTTP single requests validate outgoing headers targets and timeout options") {
    io_runner runtime;
    for (const auto& field : {arknet::http::field::cookie, arknet::http::field::content_type})
    {
        arknet::http_request operation(runtime.context);
        auto request = message();
        request.set(field, "value\r\nInjected: yes");
        auto future = operation.async_request("127.0.0.1", "1", std::move(request), asio::use_future);
        check(await(future).first == asio::error::invalid_argument, "header injection fails before DNS");
    }
    for (const auto& host : {"host\r\nInjected: yes", ""})
    {
        arknet::http_request operation(runtime.context);
        auto future = operation.async_request(host, "1", message(), asio::use_future);
        check(await(future).first == asio::error::invalid_argument, "invalid hosts fail before DNS");
    }
    arknet::http_request operation(runtime.context);
    arknet::http_request_options options;
    options.io_timeout = 0ms;
    auto future = operation.async_request("127.0.0.1", "1", message(), options, asio::use_future);
    check(await(future).first == asio::error::invalid_argument, "zero deadlines rejected");
}
