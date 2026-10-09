// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <memory>
#include <string>
#include <utility>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/listener.hpp>
#include <arknet/base/detail/ecs.hpp>
#include <arknet/base/detail/keepalive_options.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
namespace arknet::detail
{
template<class Socket> class run_connect_op
{
public:
    using resolver_type = asio::ip::basic_resolver<typename std::remove_cvref_t<Socket>::protocol_type>;
    using endpoints_type = typename resolver_type::results_type;
    run_connect_op(std::string host, std::string service, Socket& socket)
        : host_(std::move(host)), service_(std::move(service)), socket_(socket), resolver_(std::make_unique<resolver_type>(socket.get_executor())) {}
    template<class Operation> void operator()(Operation& operation, error_code error = {}, endpoints_type endpoints = {})
    {
        if (phase_ == phase::initial)
        {
            phase_ = phase::resolving;
            resolver_->async_resolve(host_, service_, std::move(operation));
            return;
        }
        if (phase_ == phase::resolving)
        {
            if (error) { operation.complete(error); return; }
            endpoints_ = std::move(endpoints);
            position_ = endpoints_.begin();
            phase_ = phase::connecting;
        }
        else if (!error || error == asio::error::operation_aborted) { operation.complete(error); return; }
        else ++position_;
        if (position_ == endpoints_.end()) { operation.complete(error ? error : error_code(asio::error::host_unreachable)); return; }
        error_code ignored;
        socket_.close(ignored);
        socket_.async_connect(position_->endpoint(), std::move(operation));
    }
private:
    enum class phase { initial, resolving, connecting };
    phase phase_ = phase::initial;
    std::string host_, service_;
    Socket& socket_;
    std::unique_ptr<resolver_type> resolver_;
    endpoints_type endpoints_;
    typename endpoints_type::iterator position_;
};
}
namespace arknet
{
template<class Socket, class Token> auto async_connect(std::string host, std::string service, Socket& socket, Token&& token)
{
    return asio::async_compose<Token, void(error_code)>(detail::run_connect_op<Socket>(std::move(host), std::move(service), socket), token, socket);
}
}
namespace arknet::detail
{
template<class Derived, class Args, bool Session> class connect_cp_member_variables {};
template<class Derived, class Args> class connect_cp_member_variables<Derived, Args, false>
{
public:
    template<class Host> Derived& set_host(Host&& host) { host_ = to_string(std::forward<Host>(host)); return static_cast<Derived&>(*this); }
    template<class Service> Derived& set_port(Service&& service) { port_ = to_string(std::forward<Service>(service)); return static_cast<Derived&>(*this); }
    const std::string& get_host() const noexcept { return host_; }
    const std::string& get_port() const noexcept { return port_; }
protected:
    std::string host_, port_;
};
template<class Derived, class Args> class connect_cp : public connect_cp_member_variables<Derived, Args, Args::is_session>
{
public:
    using socket_t = typename Args::socket_t;
    using decay_socket_t = std::remove_cvref_t<socket_t>;
    using lowest_layer_t = typename decay_socket_t::lowest_layer_type;
    using resolver_type = asio::ip::basic_resolver<typename lowest_layer_t::protocol_type>;
    using endpoints_type = typename resolver_type::results_type;
    using endpoints_iterator = typename endpoints_type::iterator;
    using self = connect_cp;
protected:
    template<bool Async, class Match, class Chain> requires (!Args::is_session)
    void _start_connect(std::shared_ptr<Derived> owner, std::shared_ptr<ecs_t<Match>> options, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        clear_last_error();
#ifndef NDEBUG
        object.is_stop_reconnect_timer_called_ = false;
        object.is_stop_connect_timeout_timer_called_ = false;
        object.is_disconnect_called_ = false;
#endif
        if (object.state_.load() != state_t::starting) { object._handle_connect(asio::error::operation_aborted, std::move(owner), std::move(options), std::move(chain)); return; }
        object._make_reconnect_timer(owner, options);
        object._make_connect_timeout_timer(owner, object.get_connect_timeout());
        object._post_resolve(std::move(owner), std::move(options), std::move(chain));
    }
    template<class Match> std::string_view _get_real_host(std::shared_ptr<Derived>&, std::shared_ptr<ecs_t<Match>>&) { return this->host_; }
    template<class Match> std::string_view _get_real_port(std::shared_ptr<Derived>&, std::shared_ptr<ecs_t<Match>>&) { return this->port_; }
    template<class Match, class Chain> requires (!Args::is_session)
    void _post_resolve(std::shared_ptr<Derived> owner, std::shared_ptr<ecs_t<Match>> options, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        const std::string host(object._get_real_host(owner, options));
        const std::string service(object._get_real_port(owner, options));
        auto resolver = std::make_unique<resolver_type>(object.io_->executor());
        auto* operation = resolver.get();
        operation->async_resolve(host, service,
            [this, &object, owner = std::move(owner), options = std::move(options), chain = std::move(chain), resolver = std::move(resolver)](error_code error, endpoints_type endpoints) mutable
            {
                if (!object.connect_timeout_timer_) error = asio::error::timed_out;
                if (error) object._handle_connect(error, std::move(owner), std::move(options), std::move(chain));
                else
                {
                    auto owned = std::make_unique<endpoints_type>(std::move(endpoints));
                    const auto first = owned->begin();
                    object._post_connect(error, std::move(owned), first, std::move(owner), std::move(options), std::move(chain));
                }
            });
    }
    template<class Match, class Chain> requires (!Args::is_session)
    void _post_connect(error_code error, std::unique_ptr<endpoints_type> endpoints, endpoints_iterator position, std::shared_ptr<Derived> owner, std::shared_ptr<ecs_t<Match>> options, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        if (object.state_.load() != state_t::starting) { object._handle_connect(asio::error::operation_aborted, std::move(owner), std::move(options), std::move(chain)); return; }
        if (position == endpoints->end()) { object._handle_connect(error ? error : error_code(asio::error::host_unreachable), std::move(owner), std::move(options), std::move(chain)); return; }
        auto& socket = object.socket();
        error_code ignored;
        if (socket.is_open())
        {
            const auto local = socket.local_endpoint(ignored);
            if (ignored || local.protocol() != position->endpoint().protocol()) { socket.cancel(ignored); socket.close(ignored); }
        }
        if (!socket.is_open())
        {
            socket.open(position->endpoint().protocol(), error);
            if (error) { object._handle_connect(error, std::move(owner), std::move(options), std::move(chain)); return; }
            socket.set_option(asio::socket_base::reuse_address(true), ignored);
            detail::set_keepalive_options(socket);
            clear_last_error();
            object._do_init(options);
            object._fire_init();
        }
        const auto endpoint = position->endpoint();
        socket.async_connect(endpoint, make_allocator(object.rallocator(), [&object, owner = std::move(owner), options = std::move(options), chain = std::move(chain), endpoints = std::move(endpoints), position](const error_code& failure) mutable
        {
            if (failure && failure != asio::error::operation_aborted) object._post_connect(failure, std::move(endpoints), std::next(position), std::move(owner), std::move(options), std::move(chain));
            else object._finish_socket_connect(failure, std::move(owner), std::move(options), std::move(chain));
        }));
    }
    template<class Match, class Chain> void _finish_socket_connect(const error_code& error, std::shared_ptr<Derived> owner, std::shared_ptr<ecs_t<Match>> options, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        error_code ignored;
        const auto endpoint = object.socket().lowest_layer().remote_endpoint(ignored);
        if (!ignored) object.remote_endpoint_ = endpoint;
        object._handle_connect(object.state_.load() == state_t::starting ? error : error_code(asio::error::operation_aborted), std::move(owner), std::move(options), std::move(chain));
    }
    template<class Match, class Chain> void _handle_connect(const error_code& error, std::shared_ptr<Derived> owner, std::shared_ptr<ecs_t<Match>> options, Chain chain)
    {
        set_last_error(error);
        static_cast<Derived&>(*this)._done_connect(error, std::move(owner), std::move(options), std::move(chain));
    }
    template<class Match, class Chain> void _done_connect(error_code error, std::shared_ptr<Derived> owner, std::shared_ptr<ecs_t<Match>> options, Chain chain)
    {
        auto& object = static_cast<Derived&>(*this);
        if constexpr (Args::is_session) { if (!object.socket().is_open()) error = asio::error::timed_out; }
        else if (!object.connect_timeout_timer_) error = asio::error::timed_out;
        if (!error)
        {
            if constexpr (!Args::is_session) object.reset_life_id();
            auto expected = state_t::starting;
            if (!object.state_.compare_exchange_strong(expected, state_t::started)) error = asio::error::operation_aborted;
        }
        set_last_error(error);
        if constexpr (Args::is_session) { if (!error && object.state_.load() == state_t::started) object._fire_connect(owner, options); }
        else if (object.state_.load() != state_t::stopped) object._fire_connect(owner, options);
        if (!error && object.state_.load() != state_t::started) error = asio::error::operation_aborted;
        object._stop_connect_timeout_timer();
        set_last_error(error);
        if (error)
        {
            // Publish the failed connect completion before disconnect can change last_error.
            { auto completion = chain.move_event(); }
            object._do_disconnect(error, std::move(owner), defer_event(chain.move_guard()));
        }
        else object._do_start(std::move(owner), std::move(options), std::move(chain));
    }
};
}
