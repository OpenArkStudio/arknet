// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <utility>
#include <arknet/external/asio.hpp>
#include <arknet/external/beast.hpp>
namespace arknet
{
using rate_policy_access = beast::rate_policy_access;
using unlimited_rate_policy = beast::unlimited_rate_policy;
using simple_rate_policy = beast::simple_rate_policy;
template <class Protocol, class Executor = asio::any_io_executor, class RatePolicy = unlimited_rate_policy>
class basic_stream : public beast::basic_stream<Protocol, Executor, RatePolicy>
{
public:
    using super = beast::basic_stream<Protocol, Executor, RatePolicy>;
    using super::super;
    using protocol_type = Protocol;
    using endpoint_type = typename Protocol::endpoint;
    using native_handle_type = typename super::socket_type::native_handle_type;
    using lowest_layer_type = typename super::socket_type::lowest_layer_type;
    lowest_layer_type& lowest_layer() noexcept { return this->socket().lowest_layer(); }
    const lowest_layer_type& lowest_layer() const noexcept { return this->socket().lowest_layer(); }
    bool is_open() const noexcept { return this->socket().is_open(); }
    void open(const Protocol& protocol = Protocol()) { this->socket().open(protocol); }
    template <class... Args> decltype(auto) open(Args&&... args)
    {
        return this->socket().open(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) assign(Args&&... args)
    {
        return this->socket().assign(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) release(Args&&... args)
    {
        return this->socket().release(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) native_handle(Args&&... args)
    {
        return this->socket().native_handle(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) cancel(Args&&... args)
    {
        return this->socket().cancel(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) bind(Args&&... args)
    {
        return this->socket().bind(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) io_control(Args&&... args)
    {
        return this->socket().io_control(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) non_blocking(Args&&... args)
    {
        return this->socket().non_blocking(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) native_non_blocking(Args&&... args)
    {
        return this->socket().native_non_blocking(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) shutdown(Args&&... args)
    {
        return this->socket().shutdown(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) wait(Args&&... args)
    {
        return this->socket().wait(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) async_wait(Args&&... args)
    {
        return this->socket().async_wait(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) set_option(Args&&... args)
    {
        return this->socket().set_option(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) at_mark(Args&&... args) const
    {
        return this->socket().at_mark(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) available(Args&&... args) const
    {
        return this->socket().available(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) local_endpoint(Args&&... args) const
    {
        return this->socket().local_endpoint(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) remote_endpoint(Args&&... args) const
    {
        return this->socket().remote_endpoint(std::forward<Args>(args)...);
    }
    template <class... Args> decltype(auto) get_option(Args&&... args) const
    {
        return this->socket().get_option(std::forward<Args>(args)...);
    }
    void close() { super::close(); }
    void close(error_code& error) { this->socket().close(error); }
};
template <class Protocol, class Executor, class RatePolicy>
void teardown(beast::role_type role, basic_stream<Protocol, Executor, RatePolicy>& stream, error_code& error)
{
    beast::websocket::teardown(role, stream.socket(), error);
}
template <class Protocol, class Executor, class RatePolicy, class Handler>
void async_teardown(beast::role_type role, basic_stream<Protocol, Executor, RatePolicy>& stream, Handler&& handler)
{
    beast::websocket::async_teardown(role, stream.socket(), std::forward<Handler>(handler));
}
}
