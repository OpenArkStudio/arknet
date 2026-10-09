// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/base/server.hpp>
#include <arknet/udp/udp_session.hpp>
#include <vector>

namespace arknet::detail
{
ARKNET_CLASS_FORWARD_DECLARE_BASE;
ARKNET_CLASS_FORWARD_DECLARE_UDP_BASE;
ARKNET_CLASS_FORWARD_DECLARE_UDP_SERVER;

template <class Derived, class Session> class udp_server_impl_t : public server_impl_t<Derived, Session>, public udp_tag
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_SERVER;
    using base = server_impl_t<Derived, Session>;

public:
    using super = base;
    using self = udp_server_impl_t;
    using session_type = Session;

    explicit udp_server_impl_t(std::size_t initial = udp_frame_size, std::size_t maximum = max_buffer_size,
                               std::size_t workers = 1)
        : base(workers), acceptor_(std::make_shared<asio::ip::udp::socket>(this->io_->executor())),
          buffer_(initial, maximum)
    {
    }

    template <class Scheduler>
        requires(!std::is_integral_v<remove_cvref_t<Scheduler>>)
    udp_server_impl_t(std::size_t initial, std::size_t maximum, Scheduler&& scheduler)
        : base(std::forward<Scheduler>(scheduler)),
          acceptor_(std::make_shared<asio::ip::udp::socket>(this->io_->executor())), buffer_(initial, maximum)
    {
    }

    template <class Scheduler>
        requires(!std::is_integral_v<remove_cvref_t<Scheduler>>)
    explicit udp_server_impl_t(Scheduler&& scheduler)
        : udp_server_impl_t(udp_frame_size, max_buffer_size, std::forward<Scheduler>(scheduler))
    {
    }

    ~udp_server_impl_t() { stop(); }

    template <class Host, class Service, class... Options>
    bool start(Host&& host, Service&& service, Options&&... options)
    {
        return this->derived()._do_start(std::forward<Host>(host), std::forward<Service>(service),
                                         ecs_helper::make_ecs('0', std::forward<Options>(options)...));
    }

    void stop()
    {
        if (is_stopped())
        {
            this->stop_iopool();
            return;
        }
        if (this->is_iopool_stopped())
            return;
        auto& owner = this->derived();
        owner.io_->unregobj(&owner);
        auto promise = std::make_shared<std::promise<void>>();
        auto completion = promise->get_future();
        owner.post(
            [&owner, lifetime = owner.selfptr(), promise]() mutable
            {
                if (owner.is_stopped())
                {
                    promise->set_value();
                    return;
                }
                owner.stop_waiters_.push_back(std::move(promise));
                owner._do_stop(asio::error::operation_aborted, std::move(lifetime));
            });
        const bool may_wait = !this->iopool().running_in_threads();
        if (may_wait && this->is_external_iopool())
            completion.get();
        this->stop_iopool();
        if (may_wait && !this->is_external_iopool())
            completion.get();
    }

    void destroy()
    {
        this->acceptor_.reset();
        base::destroy();
    }
    bool is_started() const { return this->state_ == state_t::started && this->acceptor_->is_open(); }
    bool is_stopped() const { return this->state_ == state_t::stopped; }
    asio::ip::udp::socket& acceptor() noexcept { return *acceptor_; }
    const asio::ip::udp::socket& acceptor() const noexcept { return *acceptor_; }

    template <class Function, class... Object> Derived& bind_recv(Function&& function, Object&&... object)
    {
        this->listener_.bind(event_type::recv, observer_t<std::shared_ptr<Session>&, std::string_view>(
                                                   std::forward<Function>(function), std::forward<Object>(object)...));
        return this->derived();
    }
    template <class Function, class... Object> Derived& bind_connect(Function&& function, Object&&... object)
    {
        return bind_session_notification(event_type::connect, std::forward<Function>(function),
                                         std::forward<Object>(object)...);
    }
    template <class Function, class... Object> Derived& bind_disconnect(Function&& function, Object&&... object)
    {
        return bind_session_notification(event_type::disconnect, std::forward<Function>(function),
                                         std::forward<Object>(object)...);
    }
    template <class Function, class... Object> Derived& bind_init(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::init, std::forward<Function>(function), std::forward<Object>(object)...);
    }
    template <class Function, class... Object> Derived& bind_start(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::start, std::forward<Function>(function), std::forward<Object>(object)...);
    }
    template <class Function, class... Object> Derived& bind_stop(Function&& function, Object&&... object)
    {
        return bind_notification(event_type::stop, std::forward<Function>(function), std::forward<Object>(object)...);
    }

protected:
    template <class Host, class Service, class Condition>
    bool _do_start(Host&& host, Service&& service, std::shared_ptr<ecs_t<Condition>> condition)
    {
        if (buffer_.max_size() < udp_frame_size)
        {
            set_last_error(asio::error::message_size);
            return false;
        }
        auto host_text = detail::to_string(std::forward<Host>(host));
        auto service_text = detail::to_string(std::forward<Service>(service));
        this->start_iopool();
        if (!this->is_iopool_started())
        {
            set_last_error(asio::error::operation_aborted);
            return false;
        }
        auto promise = std::make_shared<std::promise<error_code>>();
        auto completion = promise->get_future();
        auto& owner = this->derived();
        owner.post(
            [&owner, lifetime = owner.selfptr(), condition = std::move(condition), promise, host = std::move(host_text),
             service = std::move(service_text)]() mutable
            {
                if (owner.state_ != state_t::stopped)
                {
                    promise->set_value(asio::error::already_started);
                    return;
                }
                owner.io_->init_thread_id();
                owner.io_->regobj(&owner);
                owner.state_ = state_t::starting;
                owner.ecs_ = condition;
                // The last session or receive completion releases this stop barrier.
                owner.counter_ptr_ = std::shared_ptr<void>(new char,
                                                           [&owner, lifetime](void* memory) mutable
                                                           {
                                                               delete static_cast<char*>(memory);
                                                               owner._exec_stop(owner.stop_error_, std::move(lifetime));
                                                           });
                error_code error;
                asio::ip::udp::resolver resolver(owner.io_->executor());
                auto endpoints = resolver.resolve(host, service, asio::ip::resolver_base::passive, error);
                if (!error && endpoints.empty())
                    error = asio::error::host_not_found;
                if (!error)
                {
                    auto endpoint = endpoints.begin()->endpoint();
                    owner.acceptor_->open(endpoint.protocol(), error);
                    if (!error)
                    {
                        error_code ignored;
                        owner.acceptor_->set_option(asio::socket_base::reuse_address(true), ignored);
                        clear_last_error();
                        owner._fire_init();
                        owner.acceptor_->bind(endpoint, error);
                    }
                }
                owner._handle_start(error, std::move(lifetime), std::move(condition));
                promise->set_value(error);
            });
        if (this->iopool().running_in_threads())
        {
            set_last_error(asio::error::in_progress);
            return owner.is_started();
        }
        set_last_error(completion.get());
        return !get_last_error();
    }

    template <class Condition>
    void _handle_start(error_code error, std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition)
    {
        if (!error)
            this->state_ = state_t::started;
        set_last_error(error);
        this->derived()._fire_start();
        if (error || this->state_ != state_t::started)
        {
            this->derived()._do_stop(error ? error : error_code(asio::error::operation_aborted), std::move(lifetime));
            return;
        }
        buffer_.consume(buffer_.size());
        buffer_.pre_size(udp_frame_size);
        this->derived()._post_recv(std::move(lifetime), std::move(condition));
    }

    void _do_stop(const error_code& error, std::shared_ptr<Derived>)
    {
        if (this->state_ == state_t::stopping || this->state_ == state_t::stopped)
            return;
        this->state_ = state_t::stopping;
        stop_error_ = error;
        error_code ignored;
        this->acceptor_->cancel(ignored);
        this->acceptor_->close(ignored);
        this->sessions_.for_each([](std::shared_ptr<Session>& session) { session->stop(); });
        this->counter_ptr_.reset();
    }

    void _exec_stop(const error_code& error, std::shared_ptr<Derived> lifetime)
    {
        this->derived().post(
            [this, error, lifetime = std::move(lifetime)]() mutable
            {
                this->state_ = state_t::stopped;
                this->derived()._handle_stop(error, std::move(lifetime));
            });
    }
    void _handle_stop(const error_code& error, std::shared_ptr<Derived>)
    {
        base::stop();
        set_last_error(error);
        this->derived()._fire_stop();
        auto waiters = std::exchange(stop_waiters_, {});
        for (const auto& waiter : waiters)
            waiter->set_value();
    }

    template <class Condition>
    void _post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> condition)
    {
        if (!is_started())
            return;
        this->acceptor_->async_receive_from(
            buffer_.prepare(udp_frame_size), remote_endpoint_,
            make_allocator(this->rallocator_,
                           [this, lifetime = std::move(lifetime), condition = std::move(condition),
                            barrier = this->counter_ptr_](const error_code& error, std::size_t count) mutable
                           { this->derived()._handle_recv(error, count, std::move(lifetime), std::move(condition)); }));
    }
    template <class Condition>
    void _handle_recv(const error_code& error, std::size_t count, std::shared_ptr<Derived> lifetime,
                      std::shared_ptr<ecs_t<Condition>> condition)
    {
        set_last_error(error);
        if (!is_started())
            return;
        if (error == asio::error::operation_aborted)
        {
            this->derived()._do_stop(error, std::move(lifetime));
            return;
        }
        if (!error)
        {
            buffer_.commit(count);
            const auto storage = buffer_.data();
            std::string_view bytes(static_cast<const char*>(storage.data()), count);
            auto session = this->sessions_.find(remote_endpoint_);
            if (session)
                session->_receive_datagram(session, condition, bytes);
            else
                this->derived()._handle_accept(error, bytes, session, condition);
            buffer_.consume(buffer_.size());
        }
        this->derived()._post_recv(std::move(lifetime), std::move(condition));
    }

    template <class... Prefix> std::shared_ptr<Session> _make_session(Prefix&&... prefix)
    {
        return std::make_shared<Session>(std::forward<Prefix>(prefix)..., this->sessions_, this->listener_, this->io_,
                                         buffer_.pre_size(), buffer_.max_size(), buffer_, acceptor_, remote_endpoint_);
    }
    template <class Condition>
    void _handle_accept(const error_code&, std::string_view bytes, std::shared_ptr<Session>,
                        std::shared_ptr<ecs_t<Condition>>& condition)
    {
        auto session = this->derived()._make_session();
        session->counter_ptr_ = this->counter_ptr_;
        session->first_datagram_.assign(bytes);
        session->start(detail::to_shared_ptr(condition->clone()));
    }

    void _fire_init() { this->listener_.notify(event_type::init); }
    void _fire_start() { this->listener_.notify(event_type::start); }
    void _fire_stop() { this->listener_.notify(event_type::stop); }

    std::shared_ptr<asio::ip::udp::socket> acceptor_;
    asio::ip::udp::endpoint remote_endpoint_;
    buffer_wrap<arknet::linear_buffer> buffer_;
    std::vector<std::shared_ptr<std::promise<void>>> stop_waiters_;
    error_code stop_error_;

private:
    template <class Function, class... Object>
    Derived& bind_notification(event_type event, Function&& function, Object&&... object)
    {
        this->listener_.bind(event, observer_t<>(std::forward<Function>(function), std::forward<Object>(object)...));
        return this->derived();
    }
    template <class Function, class... Object>
    Derived& bind_session_notification(event_type event, Function&& function, Object&&... object)
    {
        this->listener_.bind(event, observer_t<std::shared_ptr<Session>&>(std::forward<Function>(function),
                                                                          std::forward<Object>(object)...));
        return this->derived();
    }
};
}

namespace arknet
{
template <class Derived, class Session> using udp_server_impl_t = detail::udp_server_impl_t<Derived, Session>;
template <class Session> class udp_server_t : public detail::udp_server_impl_t<udp_server_t<Session>, Session>
{
public:
    using detail::udp_server_impl_t<udp_server_t<Session>, Session>::udp_server_impl_t;
};
using udp_server = udp_server_t<udp_session>;
}
