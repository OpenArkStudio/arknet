// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <arknet/base/error.hpp>
#include <arknet/external/beast.hpp>

namespace arknet {
struct http_request_options
{
    std::chrono::steady_clock::duration connect_timeout = std::chrono::seconds(5);
    std::chrono::steady_clock::duration io_timeout = std::chrono::seconds(20);
    std::uint32_t header_limit = 8192;
    std::uint64_t body_limit = 8 * 1024 * 1024;
};

// A single HTTP transaction. Keep the caller's context running until completion.
class http_request
{
public:
    using request_type = http::request<http::string_body>;
    using response_type = http::response<http::string_body>;
    using result_type = std::pair<error_code, response_type>;

    explicit http_request(asio::io_context& context)
        : state_(std::make_shared<state>(context)) {}
    http_request(const http_request&) = delete;
    http_request& operator=(const http_request&) = delete;
    ~http_request() {
        cancel();
    }

    template<class Completion>
    decltype(auto) async_request(
        std::string host, request_type request, http_request_options options, Completion&& completion) {
        return async_request(std::move(host), "80", std::move(request), options, std::forward<Completion>(completion));
    }

    template<class Completion>
    decltype(auto) async_request(std::string host, request_type request, Completion&& completion) {
        return async_request(
            std::move(host), std::move(request), http_request_options{}, std::forward<Completion>(completion));
    }

    template<class Callback>
    void async_request(std::string host, std::string service, request_type request, http_request_options options,
        Callback&& callback) {
        auto handler = std::make_shared<std::decay_t<Callback>>(std::forward<Callback>(callback));
        asio::post(state_->strand, [operation = state_, host = std::move(host), service = std::move(service),
                                       request = std::move(request), options, handler = std::move(handler)]() mutable {
            operation->start(std::move(host), std::move(service), std::move(request), options,
                [handler = std::move(handler)](
                    error_code ec, response_type response) mutable { std::invoke(*handler, ec, std::move(response)); });
        });
    }

    template<class Callback>
    void async_request(std::string host, std::string service, request_type request, Callback&& callback) {
        async_request(std::move(host), std::move(service), std::move(request), http_request_options{},
            std::forward<Callback>(callback));
    }

    std::future<result_type> async_request(std::string host, std::string service, request_type request,
        http_request_options options, asio::use_future_t<>) {
        auto promise = std::make_shared<std::promise<result_type>>();
        auto future = promise->get_future();
        async_request(std::move(host), std::move(service), std::move(request), options,
            [promise = std::move(promise)](
                error_code ec, response_type response) { promise->set_value({ec, std::move(response)}); });
        return future;
    }

    std::future<result_type> async_request(
        std::string host, std::string service, request_type request, asio::use_future_t<> token) {
        return async_request(std::move(host), std::move(service), std::move(request), http_request_options{}, token);
    }

    void cancel() {
        asio::post(state_->strand, [operation = state_] { operation->cancel(); });
    }
    void stop() {
        cancel();
    }

private:
    struct state : std::enable_shared_from_this<state>
    {
        explicit state(asio::io_context& context)
            : strand(asio::make_strand(context))
            , resolver(strand)
            , socket(strand)
            , deadline(strand) {}

        void start(std::string host, std::string service, request_type message, http_request_options limits,
            std::function<void(error_code, response_type)> completion) {
            if (started)
            {
                completion(asio::error::already_started, {});
                return;
            }
            started = true;
            callback = std::move(completion);
            options = limits;
            request = std::move(message);
            if (canceled)
            {
                finish(asio::error::operation_aborted);
                return;
            }
            if (!valid_request(host, service) || options.connect_timeout <= options.connect_timeout.zero() ||
                options.io_timeout <= options.io_timeout.zero() || !options.header_limit)
            {
                finish(asio::error::invalid_argument);
                return;
            }
            if (!request.count(http::field::host))
            {
                auto authority = host.find(':') == std::string::npos ? host : "[" + host + "]";
                if (service != "80" &&
                    std::all_of(service.begin(), service.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
                {
                    authority += ":" + service;
                }
                request.set(http::field::host, authority);
            }
            request.keep_alive(false);
            request.prepare_payload();
            arm_timeout(options.connect_timeout);
            resolver.async_resolve(host, service,
                [self = shared_from_this()](error_code ec, asio::ip::tcp::resolver::results_type endpoints) {
                    if (self->done || self->fail(ec))
                    {
                        return;
                    }
                    asio::async_connect(
                        self->socket, endpoints, [self](error_code error, const asio::ip::tcp::endpoint&) {
                            if (self->done || self->fail(error))
                            {
                                return;
                            }
                            self->arm_timeout(self->options.io_timeout);
                            self->write();
                        });
                });
        }

        void cancel() {
            canceled = true;
            if (started)
            {
                finish(asio::error::operation_aborted);
            }
        }

        void write() {
            http::async_write(socket, request, [self = shared_from_this()](error_code ec, std::size_t) {
                if (!self->done && !self->fail(ec))
                {
                    self->read();
                }
            });
        }

        void read() {
            parser.emplace();
            parser->body_limit(options.body_limit);
            parser->header_limit(options.header_limit);
            parser->skip(request.method() == http::verb::head);
            http::async_read(socket, buffer, *parser, [self = shared_from_this()](error_code ec, std::size_t) {
                if (self->done || self->fail(ec))
                {
                    return;
                }
                auto response = self->parser->release();
                if (response.count(http::field::content_length) > 1)
                {
                    self->finish(http::error::bad_content_length);
                    return;
                }
                if (response.result_int() < 200)
                {
                    if (response.result() == http::status::switching_protocols)
                    {
                        self->finish(asio::error::operation_not_supported);
                    }
                    else
                    {
                        self->read();
                    }
                }
                else
                {
                    self->finish({}, std::move(response));
                }
            });
        }

        bool fail(error_code ec) {
            // Ready IO handlers may run before the timer handler after the deadline.
            if (std::chrono::steady_clock::now() >= deadline.expiry())
            {
                ec = asio::error::timed_out;
            }
            if (ec)
            {
                finish(ec);
            }
            return bool(ec);
        }

        void arm_timeout(std::chrono::steady_clock::duration duration) {
            const auto generation = ++timer_generation;
            deadline.expires_after(duration);
            deadline.async_wait([self = shared_from_this(), generation](error_code ec) {
                if (!ec && !self->done && generation == self->timer_generation)
                {
                    self->finish(asio::error::timed_out);
                }
            });
        }

        void finish(error_code ec, response_type response = {}) {
            if (done)
            {
                return;
            }
            done = true;
            resolver.cancel();
            deadline.cancel();
            error_code ignored;
            socket.close(ignored);
            auto completion = std::move(callback);
            callback = {};
            completion(ec, std::move(response));
        }

        static bool valid_header_value(std::string_view value) {
            return std::all_of(
                value.begin(), value.end(), [](unsigned char c) { return (c >= 32 && c != 127) || c == '\t'; });
        }
        static bool token_character(unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
        }
        bool valid_request(std::string_view host, std::string_view service) const {
            if (host.empty() || host.find_first_of(" \t\r\n/@?#\\[]") != std::string_view::npos ||
                !valid_header_value(host) || host.find('\0') != std::string_view::npos || service.empty() ||
                !std::all_of(service.begin(), service.end(),
                    [](unsigned char c) {
                        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
                    }) ||
                (request.version() != 10 && request.version() != 11) || request.count(http::field::host) > 1 ||
                (request.method() == http::verb::trace && !request.body().empty()) || request.method_string().empty() ||
                !std::all_of(request.method_string().begin(), request.method_string().end(), token_character) ||
                request.target().empty() ||
                std::any_of(request.target().begin(), request.target().end(),
                    [](unsigned char c) { return c <= 32 || c >= 127 || c == '#'; }))
            {
                return false;
            }
            for (const auto& field : request)
            {
                if (field.name_string().empty() || !valid_header_value(field.value()) ||
                    !std::all_of(field.name_string().begin(), field.name_string().end(), token_character))
                {
                    return false;
                }
            }
            return true;
        }

        asio::strand<asio::io_context::executor_type> strand;
        asio::ip::tcp::resolver resolver;
        asio::ip::tcp::socket socket;
        asio::steady_timer deadline;
        beast::flat_buffer buffer;
        std::optional<http::response_parser<http::string_body>> parser;
        request_type request;
        http_request_options options;
        std::function<void(error_code, response_type)> callback;
        std::size_t timer_generation = 0;
        bool started = false;
        bool canceled = false;
        bool done = false;
    };
    std::shared_ptr<state> state_;
};
} // namespace arknet
