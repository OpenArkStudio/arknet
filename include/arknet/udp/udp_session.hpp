// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/base/session.hpp>
#include <arknet/base/detail/linear_buffer.hpp>
#include <arknet/udp/impl/udp_send_op.hpp>

namespace arknet::detail
{
struct template_args_udp_session : udp_tag
{
    static constexpr bool is_session = true;
    static constexpr bool is_client = false;
    static constexpr bool is_server = false;
    using socket_t = asio::ip::udp::socket;
    using buffer_t = arknet::linear_buffer;
    using send_data_t = std::string_view;
    using recv_data_t = std::string_view;
    static constexpr std::size_t allocator_storage_size = 256;
};

ARKNET_CLASS_FORWARD_DECLARE_BASE;
ARKNET_CLASS_FORWARD_DECLARE_UDP_BASE;
ARKNET_CLASS_FORWARD_DECLARE_UDP_SERVER;
ARKNET_CLASS_FORWARD_DECLARE_UDP_SESSION;

template <class Derived, class Args = template_args_udp_session>
class udp_session_impl_t : public session_impl_t<Derived, Args>, public udp_send_op<Derived, Args>, public udp_tag
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_SERVER;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_SESSION;
    using base = session_impl_t<Derived, Args>;

public:
    using super = base;
    using self = udp_session_impl_t;
    using args_type = Args;
    using key_type = asio::ip::udp::endpoint;
    using buffer_type = typename Args::buffer_t;
    using send_data_t = typename Args::send_data_t;
    using recv_data_t = typename Args::recv_data_t;

    udp_session_impl_t(session_mgr_t<Derived>& sessions, listener_t& listener, std::shared_ptr<io_t> io,
                       std::size_t initial, std::size_t maximum, arknet::linear_buffer&,
                       std::shared_ptr<typename Args::socket_t> socket, asio::ip::udp::endpoint& endpoint)
        : base(sessions, listener, std::move(io), initial, maximum, std::move(socket))
    {
        this->remote_endpoint_ = endpoint;
        this->set_silence_timeout(std::chrono::milliseconds(udp_silence_timeout));
        this->set_connect_timeout(std::chrono::milliseconds(udp_connect_timeout));
    }

    void stop()
    {
        if (this->state_ == state_t::stopped || this->state_ == state_t::stopping)
            return;
        stop_requested_ = true;
        auto promise = std::make_shared<std::promise<void>>();
        auto completed = promise->get_future();
        this->derived()._do_disconnect(
            asio::error::operation_aborted, this->selfptr(),
            defer_event{[promise](event_queue_guard<Derived>) { promise->set_value(); }, event_queue_guard<Derived>{}});
        if (!this->io_->context().get_executor().running_in_this_thread() &&
            !this->sessions_.io_->context().get_executor().running_in_this_thread())
            completed.get();
    }

    bool is_stopped() const noexcept { return this->state_ == state_t::stopped; }
    std::string get_remote_address() const noexcept
    {
        try
        {
            auto address = this->remote_endpoint_.address().to_string();
            clear_last_error();
            return address;
        }
        catch (const system_error& error)
        {
            set_last_error(error.code());
            return {};
        }
    }
    std::string remote_address() const noexcept { return get_remote_address(); }
    unsigned short get_remote_port() const noexcept { return this->remote_endpoint_.port(); }
    unsigned short remote_port() const noexcept { return get_remote_port(); }
    key_type hash_key() const noexcept { return this->remote_endpoint_; }

protected:
    template <class Condition> void start(std::shared_ptr<ecs_t<Condition>> condition)
    {
        auto& owner = this->derived();
        owner.io_->init_thread_id();
        owner.state_ = state_t::starting;
        owner.ecs_ = condition;
        owner._do_init(condition);
        owner.state_ = state_t::started;
        clear_last_error();
        auto lifetime = owner.selfptr();
        owner._fire_connect(lifetime, condition);
        owner._do_start(std::move(lifetime), std::move(condition), defer_event<void, Derived>{});
    }

    template <class Condition> void _do_init(std::shared_ptr<ecs_t<Condition>>&)
    {
        this->reset_connect_time();
        this->update_alive_time();
    }

    template <class Condition, class Chain>
    void _do_start(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition, Chain chain)
    {
        if (!this->is_started() || stop_requested_)
            return;
        // Routing must be ready before the server posts its next receive.
        this->sessions_.emplace(
            lifetime,
            [this, lifetime, condition = std::move(condition), chain = std::move(chain)](bool added) mutable
            {
                if (!added)
                {
                    this->derived()._do_disconnect(asio::error::address_in_use, std::move(lifetime));
                    return;
                }
                this->derived()._start_recv(std::move(lifetime), std::move(condition), std::move(chain));
            });
    }

    template <class Condition, class Chain>
    void _start_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition, Chain)
    {
        if (!this->is_started() || stop_requested_)
            return;
        this->derived()._post_silence_timer(this->silence_timeout_, lifetime);
        auto first = std::move(first_datagram_);
        this->derived()._fire_recv(lifetime, condition, first);
    }

    template <class Condition>
    void _receive_datagram(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition,
                           std::string_view bytes)
    {
        if (!this->is_started() || stop_requested_)
            return;
        this->update_alive_time();
        this->derived()._fire_recv(lifetime, condition, bytes);
    }

    template <class Chain = defer_event<void, Derived>>
    void _do_disconnect(const error_code& error, std::shared_ptr<Derived> lifetime, Chain chain = {})
    {
        stop_requested_ = true;
        this->derived().disp_event(
            [this, error, lifetime = std::move(lifetime),
             callback = chain.move_event()](event_queue_guard<Derived> guard) mutable
            {
                defer_event continuation{std::move(callback), std::move(guard)};
                const auto previous = this->state_.exchange(state_t::stopping);
                if (previous == state_t::stopped)
                {
                    this->state_ = state_t::stopped;
                    return;
                }
                this->sessions_.erase(
                    lifetime,
                    [this, error, previous, lifetime, continuation = std::move(continuation)](bool erased) mutable
                    {
                        this->state_ = state_t::stopped;
                        set_last_error(error);
                        if (erased && previous == state_t::started)
                            this->derived()._fire_disconnect(lifetime);
                        this->derived()._do_stop(error, std::move(lifetime), std::move(continuation));
                    });
            },
            chain.move_guard());
    }

    template <class Chain> void _do_stop(const error_code& error, std::shared_ptr<Derived> lifetime, Chain chain)
    {
        base::stop();
        this->derived()._handle_stop(error, std::move(lifetime), std::move(chain));
    }
    template <class Chain> void _handle_stop(const error_code&, std::shared_ptr<Derived>, Chain) {}

    template <class Data, class Completion> bool _do_send(Data& data, Completion&& completion)
    {
        return this->derived()._udp_send_to(this->remote_endpoint_, data, std::forward<Completion>(completion));
    }

    template <class Condition>
    void _fire_recv(std::shared_ptr<Derived>& lifetime, std::shared_ptr<ecs_t<Condition>>&, std::string_view bytes)
    {
        this->listener_.notify(event_type::recv, lifetime,
                               detail::call_data_filter_before_recv(this->derived(), bytes));
    }
    template <class Condition>
    void _fire_connect(std::shared_ptr<Derived>& lifetime, std::shared_ptr<ecs_t<Condition>>&)
    {
        this->listener_.notify(event_type::connect, lifetime);
    }
    void _fire_disconnect(std::shared_ptr<Derived>& lifetime)
    {
        this->listener_.notify(event_type::disconnect, lifetime);
    }

    auto& rallocator() noexcept { return write_memory_; }
    auto& wallocator() noexcept { return write_memory_; }
    handler_memory<std::false_type, assizer<Args>> write_memory_;
    std::string first_datagram_;
    std::atomic<bool> stop_requested_{false};
};
}

namespace arknet
{
using udp_session_args = detail::template_args_udp_session;
template <class Derived, class Args> using udp_session_impl_t = detail::udp_session_impl_t<Derived, Args>;
template <class Derived> class udp_session_t : public detail::udp_session_impl_t<Derived>
{
public:
    using detail::udp_session_impl_t<Derived>::udp_session_impl_t;
};
class udp_session : public udp_session_t<udp_session>
{
public:
    using udp_session_t<udp_session>::udp_session_t;
};
}
