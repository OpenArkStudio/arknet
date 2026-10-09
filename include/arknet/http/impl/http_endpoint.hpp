// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/external/beast.hpp>
#include <atomic>
#include <deque>
#include <limits>
#include <optional>

namespace arknet::detail
{
template <class Derived> class http_limits_cp
{
public:
    Derived& set_http_body_limit(std::uint64_t bytes) noexcept
    {
        body_limit_ = bytes;
        return static_cast<Derived&>(*this);
    }

    Derived& set_http_header_limit(std::uint32_t bytes) noexcept
    {
        header_limit_ = bytes;
        return static_cast<Derived&>(*this);
    }

    std::uint64_t get_http_body_limit() const noexcept { return body_limit_.load(); }
    std::uint32_t get_http_header_limit() const noexcept { return header_limit_.load(); }

private:
    std::atomic<std::uint64_t> body_limit_{16 * 1024 * 1024};
    std::atomic<std::uint32_t> header_limit_{8192};
};

struct http_send_data
{
    std::string wire;
    http::verb method = http::verb::unknown;
    bool close = false;
    operator asio::const_buffer() const noexcept { return asio::buffer(wire); }
};

template <class Derived, template <class, class> class Transport, class Args>
class http_endpoint_impl_t : public Transport<Derived, Args>, public http_limits_cp<Derived>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_CLIENT;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SESSION;

    using super = Transport<Derived, Args>;
    static constexpr bool request_side = Args::is_session;
    using parser_type = http::parser<request_side, http::string_body>;

public:
    template <class... T> explicit http_endpoint_impl_t(T&&... args) : super(std::forward<T>(args)...) {}
    http_endpoint_impl_t(const http_endpoint_impl_t&) = delete;
    http_endpoint_impl_t& operator=(const http_endpoint_impl_t&) = delete;
    using recv_message_type = http::message<request_side, http::string_body>;

    ~http_endpoint_impl_t()
    {
        if constexpr (Args::is_client)
            this->stop();
    }

    template <class F, class... C> Derived& bind_recv(F&& fun, C&&... obj)
    {
        if constexpr (request_side)
            this->listener_.bind(event_type::recv, observer_t<std::shared_ptr<Derived>&, recv_message_type&>(
                                                       std::forward<F>(fun), std::forward<C>(obj)...));
        else
            this->listener_.bind(event_type::recv,
                                 observer_t<recv_message_type&>(std::forward<F>(fun), std::forward<C>(obj)...));
        return this->derived();
    }

protected:
    template <class... T> void _do_init(T&&... args)
    {
        super::_do_init(std::forward<T>(args)...);
        methods_.clear();
        parser_.reset();
        closing_ = false;
    }

    template <bool Request, class Body, class Fields>
    std::size_t _send_data_size(const http::message<Request, Body, Fields>& message)
    {
        static_assert(Request != request_side, "HTTP clients send requests; sessions send responses");
        static_assert(std::is_same_v<Body, http::string_body> || std::is_same_v<Body, http::empty_body>,
                      "HTTP sends support string_body and empty_body");
        std::size_t bytes = 32;
        auto add = [&](std::size_t count)
        {
            if (count > (std::numeric_limits<std::size_t>::max)() - bytes)
                bytes = (std::numeric_limits<std::size_t>::max)();
            else
                bytes += count;
        };
        if constexpr (std::is_same_v<Body, http::string_body>)
            add(message.body().size());
        if constexpr (Request)
            add(message.target().size());
        else
            add(message.reason().size());
        for (const auto& field : message)
        {
            add(field.name_string().size());
            add(field.value().size());
            add(4);
        }
        return bytes;
    }

    template <bool Request, class Body, class Fields>
    http_send_data _data_persistence(http::message<Request, Body, Fields> message)
    {
        auto owned = detail::call_data_filter_before_send(this->derived(), std::move(message));
        http_send_data data;
        if constexpr (Request)
            data.method = owned.method();
        else
            data.close = owned.need_eof();
        http::serializer<Request, Body, Fields> serializer(owned);
        error_code ec;
        while (!serializer.is_done())
        {
            std::size_t bytes = 0;
            serializer.next(ec,
                            [&](error_code&, const auto& buffers)
                            {
                                for (auto buffer : beast::buffers_range_ref(buffers))
                                {
                                    data.wire.append(static_cast<const char*>(buffer.data()), buffer.size());
                                    bytes += buffer.size();
                                }
                            });
            if (ec)
                throw system_error(ec);
            serializer.consume(bytes);
        }
        return data;
    }

    template <class Callback> bool _do_send(http_send_data& data, Callback&& callback)
    {
        if (closing_)
        {
            set_last_error(asio::error::operation_aborted);
            callback(get_last_error(), 0);
            return false;
        }
        if constexpr (Args::is_client)
        {
            // A HEAD parser must skip the body before any response bytes are parsed.
            if (methods_.empty() && parser_ && !parser_->got_some())
                parser_->skip(data.method == http::verb::head);
            methods_.push_back(data.method);
        }
        closing_ = data.close;
        return this->derived()._tcp_send_general(asio::buffer(data.wire),
                                                 [this, close = data.close, fn = std::forward<Callback>(callback)](
                                                     const error_code& ec, std::size_t bytes) mutable
                                                 {
                                                     // Schedule shutdown before releasing the queue guard to block
                                                     // later writes.
                                                     if (close && !ec)
                                                         this->derived()._do_disconnect(asio::error::eof,
                                                                                        this->selfptr());
                                                     fn(ec, bytes);
                                                 });
    }

    template <class C> void _post_recv(std::shared_ptr<Derived> self, std::shared_ptr<ecs_t<C>> ecs)
    {
        if (!this->is_started() || closing_)
            return;
        parser_.emplace();
        parser_->body_limit(this->get_http_body_limit());
        parser_->header_limit(this->get_http_header_limit());
        if constexpr (Args::is_client)
            parser_->skip(!methods_.empty() && methods_.front() == http::verb::head);
        _read_http(std::move(self), std::move(ecs), true);
    }

private:
    template <class C> void _read_http(std::shared_ptr<Derived> self, std::shared_ptr<ecs_t<C>> ecs, bool header)
    {
        ARKNET_ASSERT(!this->reading_);
        this->reading_ = true;
#ifndef NDEBUG
        this->post_recv_counter_++;
#endif
        auto completion = make_allocator(
            this->rallocator(),
            [this, self = std::move(self), ecs = std::move(ecs), header](error_code ec, std::size_t) mutable
            {
                this->reading_ = false;
#ifndef NDEBUG
                this->post_recv_counter_--;
#endif
                if (ec || !this->is_started())
                {
                    if (ec == http::error::end_of_stream)
                        ec = asio::error::eof;
                    if (!ec)
                        ec = asio::error::operation_aborted;
                    super::_handle_recv(ec, 0, std::move(self), std::move(ecs));
                    return;
                }
                if constexpr (request_side)
                {
                    if (header)
                    {
                        auto& request = parser_->get();
                        if (request.version() == 11 &&
                            (request.count(http::field::host) != 1 || request[http::field::host].empty()))
                        {
                            _reject_http(http::status::bad_request);
                            return;
                        }
                        const auto expect = request[http::field::expect];
                        if (!expect.empty())
                        {
                            if (request.version() != 11 || request.count(http::field::expect) != 1 ||
                                !beast::iequals(expect, "100-continue"))
                            {
                                _reject_http(http::status::expectation_failed);
                                return;
                            }
                            if (!parser_->is_done())
                                this->async_send(
                                    http::response<http::empty_body>(http::status::continue_, request.version()));
                        }
                    }
                }
                if (!parser_->is_done())
                {
                    _read_http(std::move(self), std::move(ecs), false);
                    return;
                }
                this->update_alive_time();
                clear_last_error();
                auto message = parser_->release();
                const bool keep_alive = message.keep_alive();
                if constexpr (Args::is_client)
                {
                    if (message.result_int() >= 200 && !methods_.empty())
                        methods_.pop_front();
                    if (message.result_int() == 101)
                    {
                        super::_handle_recv(asio::error::operation_not_supported, 0, std::move(self), std::move(ecs));
                        return;
                    }
                    this->listener_.notify(event_type::recv, message);
                    if (!keep_alive && message.result_int() >= 200)
                    {
                        super::_handle_recv(asio::error::eof, 0, std::move(self), std::move(ecs));
                        return;
                    }
                }
                else
                    this->listener_.notify(event_type::recv, self, message);
                if constexpr (request_side)
                {
                    if (!keep_alive)
                        return;
                }
                _post_recv(std::move(self), std::move(ecs));
            });
        if (header)
            http::async_read_header(this->stream(), this->buffer().base(), *parser_, std::move(completion));
        else
            http::async_read(this->stream(), this->buffer().base(), *parser_, std::move(completion));
    }

    void _reject_http(http::status status)
    {
        http::response<http::empty_body> response(status, parser_->get().version());
        response.keep_alive(false);
        response.prepare_payload();
        this->async_send(std::move(response));
    }

    std::optional<parser_type> parser_;
    std::deque<http::verb> methods_;
    bool closing_ = false;
};

template <class Derived, class Session, template <class, class> class Transport>
class http_server_impl_t : public Transport<Derived, Session>, public http_limits_cp<Derived>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER;
    using super = Transport<Derived, Session>;

public:
    using super::super;
    ~http_server_impl_t() { this->stop(); }

    template <class F, class... C> Derived& bind_recv(F&& fun, C&&... obj)
    {
        this->listener_.bind(event_type::recv,
                             observer_t<std::shared_ptr<Session>&, typename Session::recv_message_type&>(
                                 std::forward<F>(fun), std::forward<C>(obj)...));
        return this->derived();
    }

protected:
    template <class... T> std::shared_ptr<Session> _make_session(T&&... args)
    {
        auto session = super::_make_session(std::forward<T>(args)...);
        session->set_http_body_limit(this->get_http_body_limit());
        session->set_http_header_limit(this->get_http_header_limit());
        return session;
    }
};
}
