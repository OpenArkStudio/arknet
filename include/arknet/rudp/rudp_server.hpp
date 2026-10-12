// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <memory>
#include <stdexcept>
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
        options_ = options;
        return this->derived();
    }
    const arq::options& get_rudp_options() const noexcept { return options_; }
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
        if (!detail::rudp_is_control(bytes, 1))
            return;
        base::_handle_accept(error, bytes, std::move(session), condition);
    }
    std::shared_ptr<Session> _make_session()
    {
        const auto bytes = this->buffer_.data();
        const auto sync = detail::rudp_read_id(static_cast<const char*>(bytes.data()) + 1);
        return base::_make_session(options_, sync, detail::rudp_next_id());
    }

private:
    arq::options options_;
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
