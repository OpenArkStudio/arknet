// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
#include <arknet/tcp/tcp_session.hpp>
#include <arknet/tcp/impl/ssl_context_cp.hpp>
#include <arknet/tcp/impl/ssl_stream_cp.hpp>
namespace arknet::detail
{
template <class Derived, class Args = template_args_tcp_session>
class tcps_session_impl_t : public tcp_session_impl_t<Derived, Args>, public ssl_stream_cp<Derived, Args>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SESSION;

public:
    using super = tcp_session_impl_t<Derived, Args>;
    using self = tcps_session_impl_t;
    using args_type = Args;
    using key_type = std::size_t;
    using buffer_type = typename Args::buffer_t;
    using ssl_stream_comp = ssl_stream_cp<Derived, Args>;
    explicit tcps_session_impl_t(asio::ssl::context& context, session_mgr_t<Derived>& sessions, listener_t& events,
                                 std::shared_ptr<io_t> lane, std::size_t initial, std::size_t maximum)
        : super(sessions, events, std::move(lane), initial, maximum),
          ssl_stream_comp(context, asio::ssl::stream_base::server), ctx_(context)
    {
    }
    void destroy()
    {
        this->ssl_stream_.reset();
        super::destroy();
    }
    auto& stream() noexcept { return this->ssl_stream(); }
    const auto& stream() const noexcept { return this->ssl_stream(); }

protected:
    template <class Condition>
    void _do_init(std::shared_ptr<Derived>& lifetime, std::shared_ptr<ecs_t<Condition>>& policy)
    {
        super::_do_init(lifetime, policy);
        this->_ssl_init(policy, this->socket(), ctx_);
    }
    template <class Chain> void _post_shutdown(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        this->_ssl_stop(
            lifetime,
            defer_event{[this, ec, lifetime, event = chain.move_event()](event_queue_guard<Derived> guard) mutable {
                            super::_post_shutdown(ec, std::move(lifetime),
                                                  defer_event{std::move(event), std::move(guard)});
                        },
                        chain.move_guard()});
    }
    template <class Condition, class Chain>
    void _handle_connect(const error_code& ec, std::shared_ptr<Derived> lifetime,
                         std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        ARKNET_ASSERT(!ec);
        ARKNET_ASSERT(this->sessions_.io_->running_in_this_thread());
        this->derived().dispatch(
            [this, lifetime = std::move(lifetime), policy = std::move(policy), chain = std::move(chain)]() mutable
            {
                this->_ssl_start(lifetime, policy, this->socket(), ctx_);
                this->derived()._post_handshake(std::move(lifetime), std::move(policy), std::move(chain));
            });
    }
    void _fire_handshake(std::shared_ptr<Derived>& lifetime)
    {
        ARKNET_ASSERT(this->sessions_.io_->running_in_this_thread());
        this->listener_.notify(event_type::handshake, lifetime);
    }
    asio::ssl::context& ctx_;
};
}
namespace arknet
{
using tcps_session_args = detail::template_args_tcp_session;
template <class Derived, class Args> using tcps_session_impl_t = detail::tcps_session_impl_t<Derived, Args>;
template <class Derived> class tcps_session_t : public detail::tcps_session_impl_t<Derived>
{
public:
    using detail::tcps_session_impl_t<Derived>::tcps_session_impl_t;
};
class tcps_session : public tcps_session_t<tcps_session>
{
public:
    using tcps_session_t::tcps_session_t;
};
}
#endif
