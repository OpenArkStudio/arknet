// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arknet/rudp/rudp_session.hpp>
#include <arknet/udp/udp_server.hpp>

namespace arknet::detail
{
template <class Derived, class Session = rudp_session>
class rudp_server_impl_t : public udp_server_impl_t<Derived, Session>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_SERVER;
    using base = udp_server_impl_t<Derived, Session>;

public:
    using base::base;
    ~rudp_server_impl_t() { this->stop(); }
    Derived& set_rudp_options(const arq::options& options)
    {
        if (!this->is_stopped())
            throw std::logic_error("Configure RUDP before start");
        arq::control_block validation(0, options);
        validate_fec_mtu(options, fec_options_);
        options_ = options;
        return this->derived();
    }
    const arq::options& get_rudp_options() const noexcept { return options_; }
    Derived& set_fec_options(const fec::options& options)
    {
        if (!this->is_stopped())
            throw std::logic_error("Configure FEC before start");
        options.validate();
        validate_fec_mtu(options_, options);
        fec_options_ = options;
        return this->derived();
    }
    const fec::options& get_fec_options() const noexcept { return fec_options_; }
    Derived& set_max_sessions(std::size_t maximum)
    {
        if (!this->is_stopped())
            throw std::logic_error("Configure RUDP before start");
        if (!maximum)
            throw std::invalid_argument("RUDP session limit must be positive");
        max_sessions_ = maximum;
        return this->derived();
    }
    std::size_t get_max_sessions() const noexcept { return max_sessions_; }
    template <class Function, class... Object> Derived& bind_handshake(Function&& function, Object&&... object)
    {
        this->listener_.bind(detail::event_type::handshake,
                             detail::observer_t<std::shared_ptr<Session>&, error_code>(
                                 std::forward<Function>(function), std::forward<Object>(object)...));
        return this->derived();
    }

protected:
    template <class Condition>
    void _post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<detail::ecs_t<Condition>> condition)
    {
        if (!closing_)
            base::_post_recv(std::move(lifetime), std::move(condition));
    }
    template <class Condition>
    void _handle_recv(const error_code& error, std::size_t count, std::shared_ptr<Derived> lifetime,
                      std::shared_ptr<detail::ecs_t<Condition>> condition)
    {
        // A successful receive can already be queued when shutdown cancels the socket.
        if (!closing_)
            base::_handle_recv(error, count, std::move(lifetime), std::move(condition));
    }
    void _do_stop(const error_code& error, std::shared_ptr<Derived> lifetime)
    {
        if (closing_ || this->state_ == state_t::stopped || this->state_ == state_t::stopping)
            return;
        std::vector<std::shared_ptr<Session>> sessions;
        this->sessions_.for_each([&](auto& session) { sessions.push_back(session); });
        if (sessions.empty())
        {
            base::_do_stop(error, std::move(lifetime));
            return;
        }
        closing_ = true;
        error_code ignored;
        this->acceptor_->cancel(ignored);
        auto remaining = std::make_shared<std::size_t>(sessions.size());
        for (auto& session : sessions)
        {
            auto bytes = std::make_shared<std::string>(rudp_control(3, session->sync_, session->conversation_));
            // Complete close notices before the shared UDP socket is closed.
            this->acceptor_->async_send_to(
                asio::buffer(*bytes), session->remote_endpoint_,
                [this, lifetime, session, bytes, remaining, error](error_code, std::size_t) mutable
                {
                    if (--*remaining == 0)
                    {
                        closing_ = false;
                        base::_do_stop(error, std::move(lifetime));
                    }
                });
        }
    }
    template <class Condition>
    void _handle_accept(const error_code& error, std::string_view bytes, std::shared_ptr<Session> session,
                        std::shared_ptr<detail::ecs_t<Condition>>& condition)
    {
        if (detail::rudp_is_fec_control(bytes, 4))
        {
            const auto mtu = detail::rudp_fec_mtu(bytes, 4);
            if (fec_options_.mode == fec::policy::disabled || static_cast<unsigned char>(bytes[5]) != 1 ||
                mtu <= fec::transport::overhead + 24 || mtu > 65507)
            {
                auto rejection =
                    std::make_shared<std::string>(detail::rudp_fec_control(6, detail::rudp_read_id(bytes.data() + 1)));
                this->acceptor_->async_send_to(asio::buffer(*rejection), this->remote_endpoint_,
                                               [rejection](error_code, std::size_t) {});
                return;
            }
        }
        else if (!detail::rudp_is_control(bytes, 1))
            return;
        if (this->get_session_count() >= max_sessions_)
            return;
        base::_handle_accept(error, bytes, std::move(session), condition);
    }
    std::shared_ptr<Session> _make_session()
    {
        const auto bytes = this->buffer_.data();
        const auto sync = detail::rudp_read_id(static_cast<const char*>(bytes.data()) + 1);
        const std::string_view request(static_cast<const char*>(bytes.data()), this->buffer_.size());
        auto negotiated_options = options_;
        fec::options negotiated_fec;
        std::uint16_t requested_mtu{};
        if (detail::rudp_is_fec_control(request, 4))
        {
            requested_mtu = detail::rudp_fec_mtu(request, 4);
            negotiated_options.mtu = std::min(options_.mtu, static_cast<std::size_t>(requested_mtu));
            negotiated_fec = fec_options_;
        }
        return base::_make_session(negotiated_options, sync, detail::rudp_next_id(), negotiated_fec, requested_mtu);
    }

private:
    static void validate_fec_mtu(const arq::options& transport, const fec::options& settings)
    {
        if (settings.mode != fec::policy::disabled &&
            (transport.mtu <= fec::transport::overhead + 24 || transport.mtu > 65507))
            throw std::invalid_argument("FEC wire MTU must be between 51 and 65507 bytes");
    }
    arq::options options_;
    fec::options fec_options_;
    std::size_t max_sessions_ = 1024;
    bool closing_{};
};
}
namespace arknet
{
template <class Derived, class Session = rudp_session>
using rudp_server_impl_t = detail::rudp_server_impl_t<Derived, Session>;
template <class Session = rudp_session> class rudp_server_t : public rudp_server_impl_t<rudp_server_t<Session>, Session>
{
public:
    using rudp_server_impl_t<rudp_server_t<Session>, Session>::rudp_server_impl_t;
};
using rudp_server = rudp_server_t<>;
}
