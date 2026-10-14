// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <arknet/rudp/detail/transport.hpp>
#include <arknet/udp/udp_session.hpp>

namespace arknet::detail {

template<class Derived>
class rudp_session_impl_t : public udp_session_impl_t<Derived>
{
    template<class, class>
    friend class rudp_server_impl_t;
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_SERVER;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_SESSION;
    using base = udp_session_impl_t<Derived>;

public:
    rudp_session_impl_t(arq::options options, std::uint32_t sync, std::uint32_t conversation, fec::options fec_options,
        std::uint16_t requested_mtu, session_mgr_t<Derived>& sessions, listener_t& listener, std::shared_ptr<io_t> io,
        std::size_t initial, std::size_t maximum, arknet::linear_buffer& buffer,
        std::shared_ptr<asio::ip::udp::socket> socket, asio::ip::udp::endpoint& endpoint)
        : base(sessions, listener, std::move(io), initial, maximum, buffer, std::move(socket), endpoint)
        , options_(options)
        , fec_options_(fec_options)
        , requested_mtu_(requested_mtu)
        , sync_(sync)
        , conversation_(conversation) {}

    ~rudp_session_impl_t() {
        if (channel_)
        {
            channel_->stop();
        }
    }

    std::uint32_t conversation() const noexcept {
        return conversation_;
    }

    const arq::options& get_rudp_options() const noexcept {
        return options_;
    }

    const fec::options& get_fec_options() const noexcept {
        return fec_options_;
    }

    std::future<rudp::statistics> get_rudp_statistics() {
        if (!this->is_started())
        {
            throw std::logic_error("Read RUDP statistics while started");
        }
        return this->post(
            [this] {
                if (!channel_)
                {
                    throw std::logic_error("RUDP disconnected before statistics snapshot");
                }
                return channel_->statistics();
            },
            asio::use_future);
    }

protected:
    template<class Condition>
    void _fire_connect(std::shared_ptr<Derived>& lifetime, std::shared_ptr<detail::ecs_t<Condition>>& condition) {
        auto weak = std::weak_ptr<Derived>(lifetime);
        channel_ = std::make_shared<detail::rudp_channel>(
            this->socket_, this->remote_endpoint_, false, conversation_, options_,
            [this, weak](std::string_view message) {
                if (auto owner = weak.lock(); owner && this->is_started())
                {
                    this->listener_.notify(detail::event_type::recv, owner,
                        detail::call_data_filter_before_recv(this->derived(), message));
                }
            },
            [this, weak](error_code error) {
                if (auto owner = weak.lock())
                {
                    this->derived()._do_disconnect(error, std::move(owner));
                }
            },
            fec_options_);
        channel_->raw_send(acknowledgement());
        channel_->start();
        clear_last_error();
        this->listener_.notify(detail::event_type::handshake, lifetime, error_code{});
        if (!this->is_started() || this->stop_requested_)
        {
            return;
        }
        base::_fire_connect(lifetime, condition);
    }

    template<class Condition>
    void _receive_datagram(std::shared_ptr<Derived> lifetime, std::shared_ptr<detail::ecs_t<Condition>> condition,
        std::string_view bytes) {
        if (this->is_started() && !this->stop_requested_)
        {
            this->derived()._fire_recv(lifetime, condition, bytes);
        }
    }

    template<class Condition>
    void _fire_recv(
        std::shared_ptr<Derived>& lifetime, std::shared_ptr<detail::ecs_t<Condition>>&, std::string_view bytes) {
        auto channel = channel_;
        if (!channel)
        {
            return;
        }
        if (fec_options_.mode == fec::policy::disabled && detail::rudp_is_control(bytes, 1))
        {
            if (detail::rudp_read_id(bytes.data() + 1) == sync_)
            {
                this->update_alive_time();
                channel->raw_send(acknowledgement());
            }
        }
        else if (fec_options_.mode != fec::policy::disabled && detail::rudp_is_fec_control(bytes, 4))
        {
            if (detail::rudp_read_id(bytes.data() + 1) == sync_ && static_cast<unsigned char>(bytes[5]) == 1 &&
                detail::rudp_fec_mtu(bytes, 4) == requested_mtu_)
            {
                this->update_alive_time();
                channel->raw_send(acknowledgement());
            }
        }
        else if (detail::rudp_is_control(bytes, 3) && detail::rudp_read_id(bytes.data() + 1) == sync_ &&
                 detail::rudp_read_id(bytes.data() + 5) == conversation_)
        {
            this->update_alive_time();
            this->derived()._do_disconnect(asio::error::eof, lifetime);
        }
        else if (channel->input(bytes))
        {
            this->update_alive_time();
        }
    }

    template<class Data, class Completion>
    bool _do_send(Data& data, Completion&& completion) {
        const auto buffer = asio::buffer(data);
        auto channel = channel_;
        const auto result = channel ? channel->send({static_cast<const char*>(buffer.data()), buffer.size()}) : -3;
        error_code error;
        if (result)
        {
            error = result == -2   ? asio::error::no_buffer_space
                    : result == -3 ? asio::error::not_connected
                                   : asio::error::message_size;
        }
        set_last_error(error);
        completion(error, error ? 0 : buffer.size());
        return !error;
    }

    void _fire_disconnect(std::shared_ptr<Derived>& lifetime) {
        close_channel();
        base::_fire_disconnect(lifetime);
    }

    template<class Chain>
    void _do_stop(const error_code& error, std::shared_ptr<Derived> lifetime, Chain chain) {
        close_channel();
        base::_do_stop(error, std::move(lifetime), std::move(chain));
    }

private:
    std::string acknowledgement() const {
        return fec_options_.mode == fec::policy::disabled
                   ? detail::rudp_control(2, sync_, conversation_)
                   : detail::rudp_fec_control(5, sync_, conversation_, static_cast<std::uint16_t>(options_.mtu));
    }

    void close_channel() {
        if (!channel_)
        {
            return;
        }
        auto finish = std::make_shared<std::string>(detail::rudp_control(3, sync_, conversation_));
        if (this->socket().is_open())
        {
            this->socket().async_send_to(
                asio::buffer(*finish), this->remote_endpoint_, [finish](error_code, std::size_t) {});
        }
        channel_->stop();
        channel_.reset();
    }

    arq::options options_;
    fec::options fec_options_;
    std::uint16_t requested_mtu_{};
    std::uint32_t sync_{};
    std::uint32_t conversation_{};
    std::shared_ptr<detail::rudp_channel> channel_;
};
} // namespace arknet::detail

namespace arknet {

template<class Derived>
using rudp_session_t = detail::rudp_session_impl_t<Derived>;

class rudp_session : public rudp_session_t<rudp_session>
{
public:
    using rudp_session_t<rudp_session>::rudp_session_t;
};
} // namespace arknet
