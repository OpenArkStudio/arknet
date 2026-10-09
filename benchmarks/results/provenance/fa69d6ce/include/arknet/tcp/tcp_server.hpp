// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/base/server.hpp>
#include <arknet/tcp/tcp_session.hpp>

namespace arknet::detail {
ARKNET_CLASS_FORWARD_DECLARE_BASE;
ARKNET_CLASS_FORWARD_DECLARE_TCP_BASE;
ARKNET_CLASS_FORWARD_DECLARE_TCP_SERVER;
template<class Derived, class Session>
class tcp_server_impl_t : public server_impl_t<Derived, Session>, public tcp_tag {
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER;
public:
    using super = server_impl_t<Derived, Session>;
    using self = tcp_server_impl_t;
    using session_type = Session;
    explicit tcp_server_impl_t(std::size_t initial = tcp_frame_size,
        std::size_t maximum = max_buffer_size, std::size_t workers = default_concurrency() + 1)
        : super(workers), init_buffer_size_(initial), max_buffer_size_(maximum) { create_listener(); }
    template<class Scheduler> requires (!std::is_integral_v<std::remove_cvref_t<Scheduler>>)
    explicit tcp_server_impl_t(std::size_t initial, std::size_t maximum, Scheduler&& scheduler)
        : super(std::forward<Scheduler>(scheduler)), init_buffer_size_(initial), max_buffer_size_(maximum) {
        create_listener();
    }
    template<class Scheduler> requires (!std::is_integral_v<std::remove_cvref_t<Scheduler>>)
    explicit tcp_server_impl_t(Scheduler&& scheduler)
        : tcp_server_impl_t(tcp_frame_size, max_buffer_size, std::forward<Scheduler>(scheduler)) {}
    ~tcp_server_impl_t() { stop(); }

    template<class Host, class Port, class... Options>
    bool start(Host&& host, Port&& port, Options&&... options) {
        return this->derived()._do_start(std::forward<Host>(host), std::forward<Port>(port),
            ecs_helper::make_ecs(asio::transfer_at_least(1), std::forward<Options>(options)...));
    }
    void stop() {
        if (this->is_iopool_stopped()) return;
        if (this->io_->context().stopped() && is_stopped()) {
            this->stop_iopool();
            return;
        }
        auto& owner = this->derived();
        const bool worker = this->iopool().running_in_threads();
        const bool external = this->is_external_iopool();
        owner.io_->unregobj(&owner);
        auto completion = std::make_shared<std::promise<void>>();
        auto result = completion->get_future();
        owner.post([&owner, lifetime = owner.selfptr(), completion] {
            if (owner.is_stopped()) completion->set_value();
            else {
                owner.stop_waiters_.push_back(completion);
                owner._do_stop(asio::error::operation_aborted, owner.selfptr());
            }
        });
        if (external && !worker) result.get();
        this->stop_iopool();
        if (!external && !worker) result.get();
    }
    void destroy() {
        counter_timer_.reset();
        acceptor_timer_.reset();
        acceptor_.reset();
        super::destroy();
    }
    bool is_started() const { return super::is_started() && acceptor_->is_open(); }
    bool is_stopped() const { return this->state_ == state_t::stopped && !acceptor_->is_open(); }
    asio::ip::tcp::acceptor& acceptor() noexcept { return *acceptor_; }
    const asio::ip::tcp::acceptor& acceptor() const noexcept { return *acceptor_; }

    template<class F, class... Bound> Derived& bind_recv(F&& callback, Bound&&... bound) {
        return bind<std::shared_ptr<Session>&, std::string_view>(event_type::recv,
            std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template<class F, class... Bound> Derived& bind_accept(F&& callback, Bound&&... bound) {
        return bind<std::shared_ptr<Session>&>(event_type::accept, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template<class F, class... Bound> Derived& bind_connect(F&& callback, Bound&&... bound) {
        return bind<std::shared_ptr<Session>&>(event_type::connect, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template<class F, class... Bound> Derived& bind_disconnect(F&& callback, Bound&&... bound) {
        return bind<std::shared_ptr<Session>&>(event_type::disconnect, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template<class F, class... Bound> Derived& bind_init(F&& callback, Bound&&... bound) {
        return bind<>(event_type::init, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template<class F, class... Bound> Derived& bind_start(F&& callback, Bound&&... bound) {
        return bind<>(event_type::start, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
    template<class F, class... Bound> Derived& bind_stop(F&& callback, Bound&&... bound) {
        return bind<>(event_type::stop, std::forward<F>(callback), std::forward<Bound>(bound)...);
    }
protected:
    template<class Host, class Port, class Condition>
    bool _do_start(Host&& host, Port&& port, std::shared_ptr<ecs_t<Condition>> policy) {
        const auto hostname = detail::to_string(std::forward<Host>(host));
        const auto service = detail::to_string(std::forward<Port>(port));
        this->start_iopool();
        if (!this->is_iopool_started()) {
            set_last_error(asio::error::operation_aborted);
            return false;
        }
        auto& owner = this->derived();
        auto completion = std::make_shared<std::promise<error_code>>();
        auto result = completion->get_future();
        owner.post([this, lifetime = owner.selfptr(), policy = std::move(policy), hostname, service, completion]() mutable {
            auto& owner = this->derived();
            defer_event finish{[completion] { completion->set_value(get_last_error()); }};
            auto expected = state_t::stopped;
            if (!this->state_.compare_exchange_strong(expected, state_t::starting)) {
                set_last_error(asio::error::already_started);
                return;
            }
            owner.io_->init_thread_id();
            owner.io_->regobj(&owner);
            owner.ecs_ = policy;
#ifndef NDEBUG
            this->sessions_.is_all_session_stop_called_ = false;
            is_stop_called_ = false;
#endif
            super::start();
            this->counter_ptr_ = std::shared_ptr<void>(reinterpret_cast<void*>(1), [&owner](void*) {
                owner._exec_stop(asio::error::operation_aborted, owner.selfptr());
            });
            owner._handle_start(open_listener(hostname, service), std::move(lifetime), std::move(policy));
        });
        if (!this->iopool().running_in_threads()) {
            set_last_error(result.get());
            return !get_last_error();
        }
        set_last_error(asio::error::in_progress);
        return owner.is_started();
    }
    template<class Condition>
    void _handle_start(error_code ec, std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy) {
        ARKNET_ASSERT(this->io_->running_in_this_thread());
        auto expected = state_t::starting;
        if (!ec && !this->state_.compare_exchange_strong(expected, state_t::started))
            ec = asio::error::operation_aborted;
        set_last_error(ec);
        this->derived()._fire_start();
        if (!ec && this->state_ != state_t::started) ec = asio::error::operation_aborted;
        if (ec) this->derived()._do_stop(ec, std::move(lifetime));
        else this->derived()._post_accept(std::move(lifetime), std::move(policy));
    }
    void _do_stop(const error_code& ec, std::shared_ptr<Derived> lifetime) {
        ARKNET_ASSERT(this->io_->running_in_this_thread());
        auto previous = this->state_.load();
        while (previous == state_t::starting || previous == state_t::started) {
            if (this->state_.compare_exchange_weak(previous, state_t::stopping)) {
                this->derived()._post_stop(ec, std::move(lifetime), previous);
                return;
            }
        }
    }
    void _post_stop(const error_code& ec, std::shared_ptr<Derived> lifetime, state_t) {
        this->derived().dispatch([this, ec, lifetime = std::move(lifetime)]() mutable {
            set_last_error(ec);
            ARKNET_ASSERT(this->state_ == state_t::stopping);
            counter_timer_->expires_at((asio::steady_timer::time_point::max)());
            counter_timer_->async_wait([lifetime](error_code) {});
            this->sessions_.quick_for_each([](std::shared_ptr<Session>& session) { session->stop(); });
#ifndef NDEBUG
            this->sessions_.is_all_session_stop_called_ = true;
#endif
            if (this->counter_ptr_) this->counter_ptr_.reset();
            else this->derived()._exec_stop(ec, std::move(lifetime));
        });
    }
    void _exec_stop(const error_code& ec, std::shared_ptr<Derived> lifetime) {
        // Queue the server completion after the last session releases its counter.
        this->derived().post([this, ec, lifetime = std::move(lifetime)]() mutable {
            auto expected = state_t::stopping;
            const bool changed = this->state_.compare_exchange_strong(expected, state_t::stopped);
            ARKNET_ASSERT(changed);
            if (changed) this->derived()._handle_stop(ec, std::move(lifetime));
        });
    }
    void _handle_stop(const error_code& ec, std::shared_ptr<Derived>) {
        set_last_error(ec);
        this->derived()._fire_stop();
        detail::cancel_timer(*acceptor_timer_);
        detail::cancel_timer(*counter_timer_);
        super::stop();
        close_listener();
        ARKNET_ASSERT(this->state_ == state_t::stopped);
        for (auto& completion : stop_waiters_) completion->set_value();
        stop_waiters_.clear();
    }
    template<class... Construction> std::shared_ptr<Session> _make_session(Construction&&... construction) {
        auto lane = this->_get_io();
        if (this->iots_.size() > 1 && lane == this->_get_io(0)) lane = this->_get_io();
        return std::make_shared<Session>(std::forward<Construction>(construction)...,
            this->sessions_, this->listener_, std::move(lane), init_buffer_size_, max_buffer_size_);
    }
    template<class Condition>
    void _post_accept(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy) {
        ARKNET_ASSERT(this->io_->running_in_this_thread());
        if (!this->derived().is_started()) return;
        auto session = this->derived()._make_session();
        const auto executor = session->io_->executor();
#ifndef NDEBUG
        ARKNET_ASSERT(this->post_recv_counter_.fetch_add(1) == 0);
#endif
        acceptor_->async_accept(executor, make_allocator(this->rallocator_,
            [this, session = std::move(session), lifetime = std::move(lifetime), policy = std::move(policy)]
            (error_code ec, asio::ip::tcp::socket peer) mutable {
#ifndef NDEBUG
                this->post_recv_counter_.fetch_sub(1);
#endif
                session->socket().lowest_layer() = std::move(peer);
                this->derived()._handle_accept(ec, std::move(session), std::move(lifetime), std::move(policy));
            }));
    }
    template<class Condition> void _handle_accept(const error_code& ec, std::shared_ptr<Session> session,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy) {
        set_last_error(ec);
        if (ec == asio::error::operation_aborted || !this->derived().is_started()) return;
        session->counter_ptr_ = this->counter_ptr_;
        session->start(detail::to_shared_ptr(policy->clone()));
        if (!ec) {
            this->derived()._post_accept(std::move(lifetime), std::move(policy));
            return;
        }
        acceptor_timer_->expires_after(std::chrono::seconds(1));
        acceptor_timer_->async_wait([this, lifetime = std::move(lifetime), policy = std::move(policy)]
            (error_code retry_error) mutable {
            if (!retry_error) this->derived().post([this, lifetime = std::move(lifetime), policy = std::move(policy)]() mutable {
                this->derived()._post_accept(std::move(lifetime), std::move(policy));
            });
        });
    }
    void _fire_init() { ARKNET_ASSERT(this->io_->running_in_this_thread()); this->listener_.notify(event_type::init); }
    void _fire_start() {
#ifndef NDEBUG
        ARKNET_ASSERT(!is_stop_called_);
#endif
        this->listener_.notify(event_type::start);
    }
    void _fire_stop() {
#ifndef NDEBUG
        is_stop_called_ = true;
#endif
        this->listener_.notify(event_type::stop);
    }
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;
    std::unique_ptr<asio::steady_timer> acceptor_timer_, counter_timer_;
    std::size_t init_buffer_size_, max_buffer_size_;
    std::vector<std::shared_ptr<std::promise<void>>> stop_waiters_;
#ifndef NDEBUG
    bool is_stop_called_ = false;
#endif
private:
    void create_listener() {
        acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(this->io_->executor());
        acceptor_timer_ = std::make_unique<asio::steady_timer>(this->io_->executor());
        counter_timer_ = std::make_unique<asio::steady_timer>(this->io_->executor());
    }
    void close_listener() {
        error_code ignored;
        acceptor_->cancel(ignored);
        acceptor_->close(ignored);
    }
    error_code open_listener(const std::string& host, const std::string& service) {
        close_listener();
        error_code ec;
        asio::ip::tcp::resolver resolver(this->io_->executor());
        const auto candidates = resolver.resolve(host, service,
            asio::ip::resolver_base::passive | asio::ip::resolver_base::address_configured, ec);
        if (ec) return ec;
        if (candidates.empty()) return asio::error::host_not_found;
        const asio::ip::tcp::endpoint endpoint = *candidates.begin();
        acceptor_->open(endpoint.protocol(), ec);
        if (ec) return ec;
        acceptor_->set_option(asio::socket_base::reuse_address{true}, ec);
        if (ec) return ec;
        clear_last_error();
        this->derived()._fire_init();
        acceptor_->bind(endpoint, ec);
        if (!ec) acceptor_->listen(asio::socket_base::max_listen_connections, ec);
        return ec;
    }
    template<class... Signature, class F, class... Bound>
    Derived& bind(event_type event, F&& callback, Bound&&... bound) {
        this->listener_.bind(event, observer_t<Signature...>{std::forward<F>(callback), std::forward<Bound>(bound)...});
        return this->derived();
    }
};
}
namespace arknet {
template<class Derived, class Session> using tcp_server_impl_t = detail::tcp_server_impl_t<Derived, Session>;
template<class Session> class tcp_server_t : public detail::tcp_server_impl_t<tcp_server_t<Session>, Session> {
public: using detail::tcp_server_impl_t<tcp_server_t<Session>, Session>::tcp_server_impl_t;
};
using tcp_server = tcp_server_t<tcp_session>;
}
#if defined(ARKNET_INCLUDE_RATE_LIMIT)
#include <arknet/tcp/tcp_stream.hpp>
namespace arknet {
template<class Session> class tcp_rate_server_t : public tcp_server_impl_t<tcp_rate_server_t<Session>, Session> {
public: using tcp_server_impl_t<tcp_rate_server_t<Session>, Session>::tcp_server_impl_t;
};
using tcp_rate_server = tcp_rate_server_t<tcp_rate_session>;
}
#endif
