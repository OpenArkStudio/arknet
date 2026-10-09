// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
#include <arknet/tcp/tcp_client.hpp>
#include <arknet/tcp/impl/ssl_context_cp.hpp>
#include <arknet/tcp/impl/ssl_stream_cp.hpp>
namespace arknet::detail {
template<class Derived, class Args = template_args_tcp_client>
class tcps_client_impl_t : public ssl_context_cp<Derived, Args>,
    public tcp_client_impl_t<Derived, Args>, public ssl_stream_cp<Derived, Args> {
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_CLIENT;
public:
    using super = tcp_client_impl_t<Derived, Args>;
    using self = tcps_client_impl_t;
    using args_type = Args;
    using buffer_type = typename Args::buffer_t;
    using ssl_context_comp = ssl_context_cp<Derived, Args>;
    using ssl_stream_comp = ssl_stream_cp<Derived, Args>;
    template<class... Construction> explicit tcps_client_impl_t(asio::ssl::context::method method, Construction&&... construction)
        : ssl_context_comp(method), super(std::forward<Construction>(construction)...),
          ssl_stream_comp(*this, asio::ssl::stream_base::client) {}
    template<class... Construction> explicit tcps_client_impl_t(Construction&&... construction)
        : tcps_client_impl_t(ARKNET_DEFAULT_SSL_METHOD, std::forward<Construction>(construction)...) {}
    ~tcps_client_impl_t() { this->stop(); }
    void destroy() { this->ssl_stream_.reset(); super::destroy(); }
    auto& stream() noexcept { return this->ssl_stream(); }
    const auto& stream() const noexcept { return this->ssl_stream(); }
    template<class F, class... Bound> Derived& bind_handshake(F&& callback, Bound&&... bound) {
        this->listener_.bind(event_type::handshake, observer_t<>{std::forward<F>(callback), std::forward<Bound>(bound)...});
        return this->derived();
    }
protected:
    template<class Condition> void _do_init(std::shared_ptr<ecs_t<Condition>>& policy) {
        super::_do_init(policy);
        this->_ssl_init(policy, this->socket(), *this);
    }
    template<class Chain> void _post_shutdown(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain) {
        this->_ssl_stop(lifetime, defer_event{[this, ec, lifetime, event = chain.move_event()]
            (event_queue_guard<Derived> guard) mutable {
            super::_post_shutdown(ec, std::move(lifetime), defer_event{std::move(event), std::move(guard)});
        }, chain.move_guard()});
    }
    template<class Condition, class Chain> void _handle_connect(const error_code& ec,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        set_last_error(ec);
        if (ec) this->derived()._done_connect(ec, std::move(lifetime), std::move(policy), std::move(chain));
        else {
            this->_ssl_start(lifetime, policy, this->socket(), *this);
            this->derived()._post_handshake(std::move(lifetime), std::move(policy), std::move(chain));
        }
    }
    void _fire_handshake(std::shared_ptr<Derived>&) {
        ARKNET_ASSERT(this->io_->running_in_this_thread());
        this->listener_.notify(event_type::handshake);
    }
};
}
namespace arknet {
using tcps_client_args = detail::template_args_tcp_client;
template<class Derived, class Args> using tcps_client_impl_t = detail::tcps_client_impl_t<Derived, Args>;
template<class Derived> class tcps_client_t : public detail::tcps_client_impl_t<Derived> {
public: using detail::tcps_client_impl_t<Derived>::tcps_client_impl_t;
};
class tcps_client : public tcps_client_t<tcps_client> { public: using tcps_client_t::tcps_client_t; };
}
#endif
