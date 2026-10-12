// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

#include <arknet/rudp/detail/transport.hpp>
#include <arknet/udp/udp_client.hpp>

namespace arknet::detail
{
template <class Derived> class rudp_client_impl_t : public udp_client_t<Derived>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_CLIENT;
    using udp_base = udp_client_t<Derived>;
    struct handshake
    {
        handshake(asio::any_io_executor executor, std::uint32_t sync)
            : retry(executor), request(detail::rudp_control(1, sync)), sync(sync)
        {
        }
        asio::steady_timer retry;
        std::string request;
        std::array<char, 65536> response{};
        std::uint32_t sync;
        bool finished{};
        std::function<void(error_code, std::uint32_t)> complete;
    };

public:
    using udp_base::udp_base;
    ~rudp_client_impl_t() { stop(); }
    Derived& set_rudp_options(const arq::options& options)
    {
        if (!this->is_stopped())
            throw std::logic_error("Configure RUDP before start");
        arq::control_block validation(0, options);
        options_ = options;
        return this->derived();
    }
    const arq::options& get_rudp_options() const noexcept { return options_; }
    template <class Host, class Service> bool start(Host&& host, Service&& service)
    {
        stop_connect_ = false;
        return udp_base::start(std::forward<Host>(host), std::forward<Service>(service));
    }
    template <class Host, class Service> bool async_start(Host&& host, Service&& service)
    {
        stop_connect_ = false;
        return udp_base::async_start(std::forward<Host>(host), std::forward<Service>(service));
    }
    void stop()
    {
        stop_connect_ = true;
        if (this->state_ == detail::state_t::starting)
            this->dispatch(
                [this, lifetime = this->selfptr()]
                {
                    if (this->state_ == detail::state_t::starting)
                    {
                        error_code ignored;
                        this->socket().cancel(ignored);
                    }
                });
        udp_base::stop();
    }
    template <class Function, class... Object> Derived& bind_handshake(Function&& function, Object&&... object)
    {
        this->listener_.bind(
            detail::event_type::handshake,
            detail::observer_t<error_code>(std::forward<Function>(function), std::forward<Object>(object)...));
        return this->derived();
    }

protected:
    template <class Condition, class Chain>
    void _handle_connect(const error_code& error, std::shared_ptr<Derived> lifetime,
                         std::shared_ptr<detail::ecs_t<Condition>> condition, Chain chain)
    {
        if (error || stop_connect_.load())
        {
            udp_base::_handle_connect(error ? error : error_code(asio::error::operation_aborted), std::move(lifetime),
                                      std::move(condition), std::move(chain));
            return;
        }
        auto operation = std::make_shared<handshake>(this->io_->executor(), detail::rudp_next_id());
        handshake_ = operation;
        auto completion = std::make_shared<Chain>(std::move(chain));
        operation->complete = [this, lifetime, condition, completion,
                               sync = operation->sync](error_code error, std::uint32_t conversation) mutable
        {
            if (!error)
            {
                sync_ = sync;
                channel_ = std::make_shared<detail::rudp_channel>(
                    this->socket_, this->remote_endpoint_, true, conversation, options_,
                    [this](std::string_view message)
                    {
                        if (this->is_started())
                            this->listener_.notify(detail::event_type::recv,
                                                   detail::call_data_filter_before_recv(this->derived(), message));
                    },
                    [this](error_code failure) { this->derived()._do_disconnect(failure, this->selfptr()); });
                channel_->start();
            }
            set_last_error(error);
            this->listener_.notify(detail::event_type::handshake, error);
            if (!error && stop_connect_.load())
                error = asio::error::operation_aborted;
            udp_base::_handle_connect(error, std::move(lifetime), std::move(condition), std::move(*completion));
        };
        send_syn(operation, lifetime);
        receive_ack(std::move(operation), std::move(lifetime));
    }
    template <class Condition>
    void _fire_recv(std::shared_ptr<Derived>& lifetime, std::shared_ptr<detail::ecs_t<Condition>>&,
                    std::string_view bytes)
    {
        auto channel = channel_;
        if (!channel)
            return;
        if (detail::rudp_is_control(bytes, 3) && detail::rudp_read_id(bytes.data() + 1) == sync_ &&
            detail::rudp_read_id(bytes.data() + 5) == channel->conversation())
            this->derived()._do_disconnect(asio::error::eof, lifetime);
        else
            channel->input(bytes);
    }
    template <class Data, class Completion> bool _do_send(Data& data, Completion&& completion)
    {
        const auto buffer = asio::buffer(data);
        const auto channel = channel_;
        const auto result = channel ? channel->send({static_cast<const char*>(buffer.data()), buffer.size()}) : -3;
        error_code error;
        if (result)
            error = result == -2   ? asio::error::no_buffer_space
                    : result == -3 ? asio::error::not_connected
                                   : asio::error::message_size;
        set_last_error(error);
        completion(error, error ? 0 : buffer.size());
        return !error;
    }
    template <class Chain>
    void _handle_disconnect(const error_code& error, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        if (channel_)
        {
            auto finish = std::make_shared<std::string>(detail::rudp_control(3, sync_, channel_->conversation()));
            if (this->socket().is_open())
                this->socket().async_send(asio::buffer(*finish), [finish](error_code, std::size_t) {});
            channel_->stop();
            channel_.reset();
        }
        if (handshake_)
            handshake_->retry.cancel();
        udp_base::_handle_disconnect(error, std::move(lifetime), std::move(chain));
    }

private:
    void send_syn(const std::shared_ptr<handshake>& operation, const std::shared_ptr<Derived>& lifetime)
    {
        if (operation->finished)
            return;
        this->socket().async_send(asio::buffer(operation->request), [operation, lifetime](error_code, std::size_t) {});
        operation->retry.expires_after(std::chrono::milliseconds(200));
        operation->retry.async_wait(
            [this, operation, lifetime](error_code error)
            {
                if (!error && !operation->finished && this->state_ == detail::state_t::starting)
                    send_syn(operation, lifetime);
            });
    }
    void receive_ack(std::shared_ptr<handshake> operation, std::shared_ptr<Derived> lifetime)
    {
        this->reading_ = true;
        this->socket().async_receive(asio::buffer(operation->response),
                                     [this, operation, lifetime](error_code error, std::size_t count) mutable
                                     {
                                         this->reading_ = false;
                                         std::uint32_t conversation{};
                                         std::string_view bytes(operation->response.data(), error ? 0 : count);
                                         if (!error)
                                         {
                                             if (!detail::rudp_is_control(bytes, 2) ||
                                                 detail::rudp_read_id(bytes.data() + 1) != operation->sync ||
                                                 !(conversation = detail::rudp_read_id(bytes.data() + 5)))
                                             {
                                                 receive_ack(operation, lifetime);
                                                 return;
                                             }
                                         }
                                         operation->finished = true;
                                         operation->retry.cancel();
                                         if (handshake_ == operation)
                                             handshake_.reset();
                                         auto complete = std::move(operation->complete);
                                         complete(error, conversation);
                                     });
    }
    arq::options options_;
    std::shared_ptr<handshake> handshake_;
    std::shared_ptr<detail::rudp_channel> channel_;
    std::uint32_t sync_{};
    std::atomic<bool> stop_connect_{};
};
}
namespace arknet
{
template <class Derived> using rudp_client_t = detail::rudp_client_impl_t<Derived>;
class rudp_client : public rudp_client_t<rudp_client>
{
public:
    using rudp_client_t<rudp_client>::rudp_client_t;
};
}
