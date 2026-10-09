// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
#include <memory>
#include <openssl/x509v3.h>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/ecs.hpp>

namespace arknet::detail
{
template <class Derived, class Args> class ssl_stream_cp : public ssl_stream_tag
{
public:
    using ssl_socket_type = typename Args::socket_t;
    using ssl_stream_type = asio::ssl::stream<ssl_socket_type&>;
    using ssl_handshake_type = asio::ssl::stream_base::handshake_type;
    ssl_stream_cp(asio::ssl::context& context, ssl_handshake_type role) noexcept : ssl_ctx_(context), ssl_type_(role) {}
    ssl_stream_type& ssl_stream() noexcept
    {
        ARKNET_ASSERT(ssl_stream_);
        return *ssl_stream_;
    }
    const ssl_stream_type& ssl_stream() const noexcept
    {
        ARKNET_ASSERT(ssl_stream_);
        return *ssl_stream_;
    }

protected:
    template <class Condition>
    void _ssl_init(std::shared_ptr<ecs_t<Condition>>&, ssl_socket_type& socket, asio::ssl::context& context)
    {
        // Capture the configured identity only after the application has loaded it.
        ssl_stream_ = std::make_unique<ssl_stream_type>(socket, context);
    }
    template <class Condition>
    void _ssl_start(std::shared_ptr<Derived>&, std::shared_ptr<ecs_t<Condition>>&, ssl_socket_type&,
                    asio::ssl::context&) noexcept
    {
        ARKNET_ASSERT(static_cast<Derived&>(*this).io_->running_in_this_thread());
    }
    template <class Chain> void _ssl_stop(std::shared_ptr<Derived> lifetime, Chain chain)
    {
        auto& owner = static_cast<Derived&>(*this);
        ARKNET_ASSERT(owner.io_->running_in_this_thread());
        if (!ssl_stream_)
            return;
        owner.disp_event(
            [this, &owner, lifetime = std::move(lifetime),
             event = chain.move_event()](event_queue_guard<Derived> guard) mutable
            {
                auto timer = std::make_shared<asio::steady_timer>(owner.io_->executor());
                timer->expires_after(owner.get_disconnect_timeout());
                timer->async_wait(
                    [&owner, lifetime](error_code ec)
                    {
                        if (!ec)
                        {
                            error_code ignored;
                            owner.socket().cancel(ignored);
                            owner.socket().close(ignored);
                        }
                    });
                error_code ignored;
                owner.socket().cancel(ignored);
#ifndef NDEBUG
                ARKNET_ASSERT(owner.post_send_counter_.fetch_add(1) == 0);
#endif
                ssl_stream_->async_shutdown(
                    [&owner, lifetime = std::move(lifetime), timer,
                     chain = defer_event{std::move(event), std::move(guard)}](error_code ec) mutable
                    {
#ifndef NDEBUG
                        owner.post_send_counter_.fetch_sub(1);
#endif
                        detail::cancel_timer(*timer);
                        set_last_error(ec);
                    });
            },
            chain.move_guard());
    }
    template <class Condition, class Chain>
    void _post_handshake(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        auto& owner = static_cast<Derived&>(*this);
        ARKNET_ASSERT(owner.io_->running_in_this_thread());
        if (auto ec = _prepare_handshake())
        {
            owner._handle_handshake(ec, std::move(lifetime), std::move(policy), std::move(chain));
            return;
        }
        struct deadline
        {
            asio::steady_timer timer;
            bool expired = false;
            explicit deadline(asio::any_io_executor executor) : timer(executor) {}
        };
        auto timeout = std::make_shared<deadline>(owner.io_->executor());
        timeout->timer.expires_after(owner.get_connect_timeout());
        timeout->timer.async_wait(
            [&owner, lifetime, timeout](error_code ec)
            {
                if (ec)
                    return;
                timeout->expired = true;
                error_code ignored;
                owner.socket().cancel(ignored);
                owner.socket().close(ignored);
            });
#ifndef NDEBUG
        ARKNET_ASSERT(owner.post_send_counter_.fetch_add(1) == 0);
#endif
        ssl_stream_->async_handshake(ssl_type_,
                                     make_allocator(owner.wallocator(),
                                                    [&owner, lifetime = std::move(lifetime), policy = std::move(policy),
                                                     timeout, chain = std::move(chain)](error_code ec) mutable
                                                    {
#ifndef NDEBUG
                                                        owner.post_send_counter_.fetch_sub(1);
#endif
                                                        detail::cancel_timer(timeout->timer);
                                                        owner._handle_handshake(
                                                            timeout->expired ? error_code{asio::error::timed_out} : ec,
                                                            std::move(lifetime), std::move(policy), std::move(chain));
                                                    }));
    }
    error_code _prepare_handshake()
    {
        if constexpr (!Args::is_client)
            return {};
        else
        {
            const auto& host = static_cast<Derived&>(*this).host_;
            if (host.empty() || host.find('\0') != std::string::npos)
                return asio::error::invalid_argument;
            SSL* ssl = ssl_stream().native_handle();
            auto* identity = SSL_get0_param(ssl);
            ERR_clear_error();
            if (SSL_set_tlsext_host_name(ssl, nullptr) != 1 || X509_VERIFY_PARAM_set1_host(identity, nullptr, 0) != 1 ||
                X509_VERIFY_PARAM_set1_ip(identity, nullptr, 0) != 1)
                return _ssl_configuration_error();
            error_code parse_error;
            const auto address = asio::ip::make_address(host, parse_error);
            if (parse_error && SSL_set_tlsext_host_name(ssl, host.c_str()) != 1)
                return _ssl_configuration_error();
            if (!(SSL_get_verify_mode(ssl) & SSL_VERIFY_PEER))
                return {};
            X509_VERIFY_PARAM_set_hostflags(identity, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
            int accepted;
            if (parse_error)
                accepted = SSL_set1_host(ssl, host.c_str());
            else if (address.is_v4())
            {
                const auto bytes = address.to_v4().to_bytes();
                accepted = X509_VERIFY_PARAM_set1_ip(identity, bytes.data(), bytes.size());
            }
            else
            {
                const auto bytes = address.to_v6().to_bytes();
                accepted = X509_VERIFY_PARAM_set1_ip(identity, bytes.data(), bytes.size());
            }
            return accepted == 1 ? error_code{} : _ssl_configuration_error();
        }
    }
    static error_code _ssl_configuration_error() noexcept
    {
        const int native = static_cast<int>(ERR_get_error());
        return native ? error_code{native, asio::error::get_ssl_category()} : error_code{asio::error::invalid_argument};
    }
    template <class Condition, class Chain>
    void _session_handle_handshake(const error_code& ec, std::shared_ptr<Derived> lifetime,
                                   std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        auto& owner = static_cast<Derived&>(*this);
        owner.sessions_.dispatch(
            [&owner, ec, lifetime = std::move(lifetime), policy = std::move(policy), chain = std::move(chain)]() mutable
            {
                set_last_error(ec);
                owner._fire_handshake(lifetime);
                if (ec)
                    owner._do_disconnect(ec, std::move(lifetime), std::move(chain));
                else
                    owner._done_connect(ec, std::move(lifetime), std::move(policy), std::move(chain));
            });
    }
    template <class Condition, class Chain>
    void _client_handle_handshake(const error_code& ec, std::shared_ptr<Derived> lifetime,
                                  std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        auto& owner = static_cast<Derived&>(*this);
        set_last_error(ec);
        owner._fire_handshake(lifetime);
        owner._done_connect(ec, std::move(lifetime), std::move(policy), std::move(chain));
    }
    template <class Condition, class Chain>
    void _handle_handshake(const error_code& ec, std::shared_ptr<Derived> lifetime,
                           std::shared_ptr<ecs_t<Condition>> policy, Chain chain)
    {
        if constexpr (Args::is_session)
            static_cast<Derived&>(*this)._session_handle_handshake(ec, std::move(lifetime), std::move(policy),
                                                                   std::move(chain));
        else
            static_cast<Derived&>(*this)._client_handle_handshake(ec, std::move(lifetime), std::move(policy),
                                                                  std::move(chain));
    }
    asio::ssl::context& ssl_ctx_;
    ssl_handshake_type ssl_type_;
    std::unique_ptr<ssl_stream_type> ssl_stream_;
};
}
#endif
