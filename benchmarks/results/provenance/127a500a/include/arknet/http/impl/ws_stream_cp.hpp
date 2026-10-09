// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/external/beast.hpp>
#include <arknet/base/error.hpp>
#include <arknet/base/detail/util.hpp>

namespace arknet::detail {
template<class T, class = void>
struct is_websocket_client : std::bool_constant<requires(T& client) { client.ws_stream(); }> {};
template<class T, class = void>
struct is_websocket_server : std::bool_constant<requires(typename T::session_type& session) { session.ws_stream(); }> {};

template<class Derived, class Args>
class ws_stream_cp : public ws_stream_tag {
public:
    using ws_stream_type = typename Args::stream_t;
    ws_stream_type& ws_stream() noexcept { ARKNET_ASSERT(ws_stream_); return *ws_stream_; }
    const ws_stream_type& ws_stream() const noexcept { ARKNET_ASSERT(ws_stream_); return *ws_stream_; }
protected:
    template<class Condition, class Transport> void _ws_init(std::shared_ptr<ecs_t<Condition>>&, Transport& transport) {
        auto& owner = static_cast<Derived&>(*this);
        ws_stream_ = std::make_unique<ws_stream_type>(transport);
        ws_stream_->read_message_max(owner.buffer().max_size());
        auto limits = websocket::stream_base::timeout::suggested(Args::is_session ? beast::role_type::server : beast::role_type::client);
        limits.handshake_timeout = owner.get_connect_timeout();
        ws_stream_->set_option(limits);
    }
    template<class Condition, class Transport> void _ws_start(std::shared_ptr<Derived>&,
        std::shared_ptr<ecs_t<Condition>>&, Transport&) {
        ARKNET_ASSERT(static_cast<Derived&>(*this).io_->running_in_this_thread());
    }
    template<class Chain> void _ws_stop(std::shared_ptr<Derived> lifetime, Chain chain) {
        auto& owner = static_cast<Derived&>(*this);
        ARKNET_ASSERT(owner.io_->running_in_this_thread());
        if (!ws_stream_) return;
        owner.disp_event([this, &owner, lifetime = std::move(lifetime), event = chain.move_event()]
            (event_queue_guard<Derived> guard) mutable {
            defer_event next{std::move(event), std::move(guard)};
            websocket::stream_base::timeout limits;
            limits.handshake_timeout = owner.get_disconnect_timeout();
            limits.idle_timeout = websocket::stream_base::none();
            ws_stream_->set_option(limits);
            ws_stream_->set_option(websocket::stream_base::decorator{[](websocket::request_type&) {}});
            ws_stream_->set_option(websocket::stream_base::decorator{[](websocket::response_type&) {}});
            if (!ws_stream_->is_open()) { ws_stream_->control_callback(); return; }
            auto timer = std::make_shared<asio::steady_timer>(owner.io_->executor());
            timer->expires_after(owner.get_disconnect_timeout());
            timer->async_wait([&owner, lifetime](error_code ec) {
                if (!ec) { error_code ignored; owner.socket().cancel(ignored); owner.socket().close(ignored); }
            });
#ifndef NDEBUG
            ARKNET_ASSERT(owner.post_send_counter_.fetch_add(1) == 0);
#endif
            ws_stream_->async_close(websocket::close_code::normal,
                [this, &owner, lifetime = std::move(lifetime), timer, next = std::move(next)](error_code ec) mutable {
#ifndef NDEBUG
                    owner.post_send_counter_.fetch_sub(1);
#endif
                    detail::cancel_timer(*timer);
                    websocket::stream_base::timeout disabled;
                    disabled.handshake_timeout = websocket::stream_base::none();
                    disabled.idle_timeout = websocket::stream_base::none();
                    ws_stream_->set_option(disabled);
                    ws_stream_->control_callback();
                    set_last_error(ec);
                });
        }, chain.move_guard());
    }
    template<class Condition> void _ws_post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy) {
        auto& owner = static_cast<Derived&>(*this);
        ARKNET_ASSERT(owner.io_->running_in_this_thread());
        if (!owner.is_started()) {
            if (owner.state_ == state_t::started) owner._do_disconnect(get_last_error(), std::move(lifetime));
            return;
        }
        ARKNET_ASSERT(!owner.reading_);
        owner.reading_ = true;
#ifndef NDEBUG
        ARKNET_ASSERT(owner.post_recv_counter_.fetch_add(1) == 0);
#endif
        ws_stream_->async_read(owner.buffer().base(), make_allocator(owner.rallocator(),
            [&owner, lifetime = std::move(lifetime), policy = std::move(policy)](error_code ec, std::size_t size) mutable {
#ifndef NDEBUG
                owner.post_recv_counter_.fetch_sub(1);
#endif
                owner.reading_ = false;
                owner._handle_recv(ec, size, std::move(lifetime), std::move(policy));
            }));
    }
    template<class Condition> void _ws_handle_recv(const error_code& ec, std::size_t size,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy) {
        static_cast<Derived&>(*this)._tcp_handle_recv(ec, size, std::move(lifetime), std::move(policy));
    }
    template<class Condition> void _post_control_callback(std::shared_ptr<Derived> lifetime,
        std::shared_ptr<ecs_t<Condition>> policy) {
        auto& owner = static_cast<Derived&>(*this);
        owner.post([this, &owner, lifetime = std::move(lifetime), policy = std::move(policy)]() mutable {
            const bool tracked = bool(lifetime);
            std::weak_ptr<Derived> weak = lifetime;
            ws_stream_->control_callback([&owner, weak, tracked, policy]
                (websocket::frame_type type, beast::string_view payload) mutable {
                auto locked = weak.lock();
                if (tracked && !locked) return;
                owner._handle_control_callback(type, payload, std::move(locked), policy);
            });
        });
    }
    template<class Condition> void _handle_control_callback(websocket::frame_type type, beast::string_view payload,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy) {
        auto& owner = static_cast<Derived&>(*this);
        owner.update_alive_time();
        switch (type) {
        case websocket::frame_type::ping: owner._handle_control_ping(payload, std::move(lifetime), std::move(policy)); break;
        case websocket::frame_type::pong: owner._handle_control_pong(payload, std::move(lifetime), std::move(policy)); break;
        case websocket::frame_type::close: owner._handle_control_close(payload, std::move(lifetime), std::move(policy)); break;
        }
    }
    template<class Condition> void _handle_control_ping(beast::string_view, std::shared_ptr<Derived>, std::shared_ptr<ecs_t<Condition>>) {}
    template<class Condition> void _handle_control_pong(beast::string_view, std::shared_ptr<Derived>, std::shared_ptr<ecs_t<Condition>>) {}
    template<class Condition> void _handle_control_close(beast::string_view, std::shared_ptr<Derived> lifetime,
        std::shared_ptr<ecs_t<Condition>>) {
        auto& owner = static_cast<Derived&>(*this);
        if (owner.state_ == state_t::started) owner._do_disconnect(websocket::error::closed, std::move(lifetime));
    }
    template<class Condition, class Chain> void _post_read_upgrade_request(std::shared_ptr<Derived> lifetime,
        std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        auto& owner = static_cast<Derived&>(*this);
        owner.get_upgrade_request() = {};
        ARKNET_ASSERT(!owner.reading_);
        owner.reading_ = true;
#ifndef NDEBUG
        ARKNET_ASSERT(owner.post_recv_counter_.fetch_add(1) == 0);
#endif
        http::async_read(owner.upgrade_stream(), owner.buffer().base(), owner.get_upgrade_request(),
            make_allocator(owner.rallocator(), [&owner, lifetime = std::move(lifetime), policy = std::move(policy),
                chain = std::move(chain)](error_code ec, std::size_t) mutable {
#ifndef NDEBUG
                owner.post_recv_counter_.fetch_sub(1);
#endif
                owner.reading_ = false;
                owner._handle_read_upgrade_request(ec, std::move(lifetime), std::move(policy), std::move(chain));
            }));
    }
    template<class Condition, class Chain> void _handle_read_upgrade_request(const error_code& ec,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        auto& owner = static_cast<Derived&>(*this);
        set_last_error(ec);
        if (ec) owner._handle_upgrade(ec, std::move(lifetime), std::move(policy), std::move(chain));
        else {
            owner.update_alive_time();
            owner._post_control_callback(lifetime, policy);
            owner._post_upgrade(std::move(lifetime), std::move(policy), owner.get_upgrade_request(), std::move(chain));
        }
    }
    template<class Condition, class Message, class Chain> void _post_upgrade(std::shared_ptr<Derived> lifetime,
        std::shared_ptr<ecs_t<Condition>> policy, Message& message, Chain chain) {
        auto& owner = static_cast<Derived&>(*this);
#ifndef NDEBUG
        ARKNET_ASSERT(owner.post_send_counter_.fetch_add(1) == 0);
#endif
        auto completion = make_allocator(owner.wallocator(), [&owner, lifetime = std::move(lifetime),
            policy = std::move(policy), chain = std::move(chain)](error_code ec) mutable {
#ifndef NDEBUG
            owner.post_send_counter_.fetch_sub(1);
#endif
            owner._handle_upgrade(ec, std::move(lifetime), std::move(policy), std::move(chain));
        });
        if constexpr (Args::is_session) ws_stream_->async_accept(message, std::move(completion));
        else ws_stream_->async_handshake(message, owner.host_, owner.get_upgrade_target(), std::move(completion));
    }
    template<class Condition, class Chain> void _post_upgrade(std::shared_ptr<Derived> lifetime,
        std::shared_ptr<ecs_t<Condition>> policy, Chain chain) requires(Args::is_session) {
        auto& owner = static_cast<Derived&>(*this);
        ws_stream_->async_accept(make_allocator(owner.wallocator(), [&owner, lifetime = std::move(lifetime),
            policy = std::move(policy), chain = std::move(chain)](error_code ec) mutable {
            owner._handle_upgrade(ec, std::move(lifetime), std::move(policy), std::move(chain));
        }));
    }
    template<class Condition, class Chain> void _session_handle_upgrade(const error_code& ec,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        auto& owner = static_cast<Derived&>(*this);
        owner.sessions_.dispatch([&owner, ec, lifetime = std::move(lifetime), policy = std::move(policy),
            chain = std::move(chain)]() mutable {
            set_last_error(ec);
            owner._fire_upgrade(lifetime);
            if (ec) owner._do_disconnect(ec, std::move(lifetime), std::move(chain));
            else owner._done_connect(ec, std::move(lifetime), std::move(policy), std::move(chain));
        });
    }
    template<class Condition, class Chain> void _client_handle_upgrade(const error_code& ec,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        auto& owner = static_cast<Derived&>(*this);
        set_last_error(ec);
        owner._fire_upgrade(lifetime);
        owner._done_connect(ec, std::move(lifetime), std::move(policy), std::move(chain));
    }
    template<class Condition, class Chain> void _handle_upgrade(const error_code& ec,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        if constexpr (Args::is_session)
            static_cast<Derived&>(*this)._session_handle_upgrade(ec, std::move(lifetime), std::move(policy), std::move(chain));
        else
            static_cast<Derived&>(*this)._client_handle_upgrade(ec, std::move(lifetime), std::move(policy), std::move(chain));
    }
    std::unique_ptr<ws_stream_type> ws_stream_;
};
}
