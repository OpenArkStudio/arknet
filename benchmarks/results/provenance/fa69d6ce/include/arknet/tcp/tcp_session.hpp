// Copyright (c) 2026 OpenArkStudio.
// Distributed under the Boost Software License, Version 1.0. See LICENSE.
#pragma once
#include <arknet/base/session.hpp>
#include <arknet/tcp/impl/tcp_keepalive_cp.hpp>
#include <arknet/tcp/impl/tcp_send_op.hpp>
#include <arknet/tcp/impl/tcp_recv_op.hpp>

namespace arknet::detail {
struct template_args_tcp_session : tcp_tag {
    static constexpr bool is_session = true, is_client = false, is_server = false;
    using socket_t = asio::ip::tcp::socket;
    using buffer_t = asio::streambuf;
    using send_data_t = std::string_view;
    using recv_data_t = std::string_view;
};
ARKNET_CLASS_FORWARD_DECLARE_BASE;
ARKNET_CLASS_FORWARD_DECLARE_TCP_BASE;
ARKNET_CLASS_FORWARD_DECLARE_TCP_SERVER;
ARKNET_CLASS_FORWARD_DECLARE_TCP_SESSION;

template<class Derived, class Args = template_args_tcp_session>
class tcp_session_impl_t : public session_impl_t<Derived, Args>,
    public tcp_keepalive_cp<Derived, Args>, public tcp_send_op<Derived, Args>,
    public tcp_recv_op<Derived, Args>, public tcp_tag {
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SESSION;
public:
    using super = session_impl_t<Derived, Args>;
    using self = tcp_session_impl_t;
    using args_type = Args;
    using key_type = std::size_t;
    using buffer_type = typename Args::buffer_t;
    using send_data_t = typename Args::send_data_t;
    using recv_data_t = typename Args::recv_data_t;
    explicit tcp_session_impl_t(session_mgr_t<Derived>& sessions, listener_t& events,
        std::shared_ptr<io_t> lane, std::size_t initial, std::size_t maximum)
        : super(sessions, events, lane, initial, maximum, lane->executor()) {
        this->set_silence_timeout(std::chrono::milliseconds(tcp_silence_timeout));
        this->set_connect_timeout(std::chrono::milliseconds(tcp_connect_timeout));
    }
    key_type hash_key() const noexcept { return reinterpret_cast<key_type>(this); }
    void stop() {
        const auto state = this->state_.load();
        if (state == state_t::stopped || state == state_t::stopping) return;
        stop_requested_ = true;
        auto& owner = this->derived();
        if (owner.sessions_.io_->running_in_this_thread() && state == state_t::starting) {
            owner.set_linger(true, 0);
            error_code ignored;
            owner.socket().close(ignored);
        }
        owner.dispatch([&owner, lifetime = owner.selfptr()] { owner._arm_stop_deadline(); });
        auto completion = std::make_shared<std::promise<void>>();
        auto result = completion->get_future();
        owner.post_event([&owner, lifetime = owner.selfptr(), completion](event_queue_guard<Derived> guard) mutable {
            owner._do_disconnect(asio::error::operation_aborted, std::move(lifetime), defer_event{
                [completion](event_queue_guard<Derived>) { completion->set_value(); }, std::move(guard)});
        });
        // Different strands can share one worker, so check context membership before waiting.
        while (!owner.io_->context().get_executor().running_in_this_thread() &&
            !owner.sessions_.io_->context().get_executor().running_in_this_thread()) {
            if (result.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
                result.get();
                break;
            }
            if (owner.io_->context().stopped() || owner.get_thread_id() == std::thread::id{} ||
                owner.sessions_.io_->get_thread_id() == std::thread::id{}) break;
        }
    }
protected:
    template<class Condition> void start(std::shared_ptr<ecs_t<Condition>> policy) {
        ARKNET_ASSERT(this->sessions_.io_->running_in_this_thread());
        auto& owner = this->derived();
        const error_code accept_error = get_last_error();
        auto lifetime = owner.selfptr();
        error_code endpoint_error;
        auto peer = owner.socket().lowest_layer().remote_endpoint(endpoint_error);
        if (!endpoint_error) this->remote_endpoint_ = peer;
        auto expected = state_t::stopped;
        stop_requested_ = false;
        if (!this->state_.compare_exchange_strong(expected, state_t::starting)) {
            owner._do_disconnect(asio::error::already_started, std::move(lifetime));
            return;
        }
#ifndef NDEBUG
        this->is_stop_silence_timer_called_ = false;
        this->is_stop_connect_timeout_timer_called_ = false;
        is_disconnect_called_ = false;
#endif
        owner.ecs_ = policy;
        owner._do_init(lifetime, policy);
        set_last_error(accept_error);
        owner._fire_accept(lifetime);
        if (accept_error || stop_requested_ || this->state_ != state_t::starting || !owner.socket().is_open()) {
            owner._do_disconnect(accept_error ? accept_error : error_code(asio::error::operation_aborted),
                std::move(lifetime));
            return;
        }
        super::start();
        owner.push_event([&owner, lifetime = std::move(lifetime), policy = std::move(policy)]
            (event_queue_guard<Derived> guard) mutable {
            owner.sessions_.dispatch([&owner, lifetime = std::move(lifetime), policy = std::move(policy),
                guard = std::move(guard)]() mutable {
                owner._handle_connect(error_code{}, std::move(lifetime), std::move(policy), defer_event{std::move(guard)});
            });
        });
    }
    template<class Condition>
    void _do_init(std::shared_ptr<Derived>& lifetime, std::shared_ptr<ecs_t<Condition>>& policy) noexcept {
        this->derived().reset_connect_time();
        this->derived().update_alive_time();
        dgram_ = std::is_same_v<typename ecs_t<Condition>::condition_lowest_type, use_dgram_t>;
        if constexpr (requires { policy->get_condition().lowest().init(lifetime); })
            policy->get_condition().lowest().init(lifetime);
        this->derived().set_keep_alive_options();
    }
    template<class Condition, class Chain>
    void _do_start(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        this->derived().disp_event([this, lifetime = std::move(lifetime), policy = std::move(policy),
            event = chain.move_event()](event_queue_guard<Derived> guard) mutable {
            auto& owner = this->derived();
            defer_event next{std::move(event), std::move(guard)};
            if (stop_requested_ || !owner.is_started())
                owner._do_disconnect(asio::error::operation_aborted, std::move(lifetime), std::move(next));
            else
                owner._join_session(std::move(lifetime), std::move(policy), std::move(next));
        }, chain.move_guard());
    }
    template<class Condition, class Chain>
    void _join_session(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        this->sessions_.emplace(lifetime, [this, lifetime, policy = std::move(policy), chain = std::move(chain)]
            (bool inserted) mutable {
            if (inserted)
                this->derived()._start_recv(std::move(lifetime), std::move(policy), std::move(chain));
            else
                this->derived()._do_disconnect(asio::error::address_in_use, std::move(lifetime), std::move(chain));
        });
    }
    template<class Chain> void _handle_disconnect(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain) {
        error_code ignored, option_error;
        auto& socket = this->socket();
        if (socket.is_open()) {
            asio::socket_base::linger linger;
            socket.lowest_layer().get_option(linger, option_error);
            if (!option_error && !(linger.enabled() && linger.timeout() == 0))
                socket.shutdown(asio::socket_base::shutdown_both, ignored);
            socket.cancel(ignored);
            socket.close(ignored);
        }
        super::_handle_disconnect(ec, std::move(lifetime), std::move(chain));
    }
    template<class Chain> void _do_stop(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain) {
        this->derived()._post_stop(ec, std::move(lifetime), std::move(chain));
    }
    template<class Chain> void _post_stop(const error_code& ec, std::shared_ptr<Derived> lifetime, Chain chain) {
        super::stop();
        this->derived()._handle_stop(ec, std::move(lifetime), std::move(chain));
    }
    template<class Chain> void _handle_stop(const error_code&, std::shared_ptr<Derived>, Chain) {
        ARKNET_ASSERT(this->state_ == state_t::stopped);
    }
    template<class Condition, class Chain>
    void _start_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy, Chain chain) {
        this->derived().dispatch([this, lifetime = std::move(lifetime), policy = std::move(policy),
            chain = std::move(chain)]() mutable {
            if constexpr (!std::is_same_v<typename ecs_t<Condition>::condition_lowest_type, hook_buffer_t>)
                this->buffer().consume(this->buffer().size());
            this->derived()._post_silence_timer(this->silence_timeout_, lifetime);
            this->derived()._post_recv(std::move(lifetime), std::move(policy));
        });
    }
    template<class Data, class F> bool _do_send(Data& data, F&& completion) {
        return this->derived()._tcp_send(data, std::forward<F>(completion));
    }
    template<class Condition> void _post_recv(std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy) {
        this->derived()._tcp_post_recv(std::move(lifetime), std::move(policy));
    }
    template<class Condition> void _handle_recv(const error_code& ec, std::size_t size,
        std::shared_ptr<Derived> lifetime, std::shared_ptr<ecs_t<Condition>> policy) {
        this->derived()._tcp_handle_recv(ec, size, std::move(lifetime), std::move(policy));
    }
    template<class Condition> void _fire_recv(std::shared_ptr<Derived>& lifetime,
        std::shared_ptr<ecs_t<Condition>>&, std::string_view data) {
        this->listener_.notify(event_type::recv, lifetime, detail::call_data_filter_before_recv(this->derived(), data));
    }
    void _fire_accept(std::shared_ptr<Derived>& lifetime) {
        ARKNET_ASSERT(this->sessions_.io_->running_in_this_thread());
        this->listener_.notify(event_type::accept, lifetime);
    }
    template<class Condition> void _fire_connect(std::shared_ptr<Derived>& lifetime, std::shared_ptr<ecs_t<Condition>>&) {
        ARKNET_ASSERT(this->sessions_.io_->running_in_this_thread());
#ifndef NDEBUG
        ARKNET_ASSERT(!is_disconnect_called_);
#endif
        this->listener_.notify(event_type::connect, lifetime);
    }
    void _fire_disconnect(std::shared_ptr<Derived>& lifetime) {
        ARKNET_ASSERT(this->sessions_.io_->running_in_this_thread());
#ifndef NDEBUG
        is_disconnect_called_ = true;
#endif
        this->listener_.notify(event_type::disconnect, lifetime);
    }
    auto& rallocator() noexcept { return rallocator_; }
    auto& wallocator() noexcept { return wallocator_; }
    handler_memory<std::true_type, assizer<Args>> rallocator_;
    handler_memory<std::false_type, assizer<Args>> wallocator_;
    std::atomic<bool> stop_requested_{false};
    bool dgram_ = false;
#ifndef NDEBUG
    bool is_disconnect_called_ = false;
#endif
};
}

namespace arknet {
using tcp_session_args = detail::template_args_tcp_session;
template<class Derived, class Args> using tcp_session_impl_t = detail::tcp_session_impl_t<Derived, Args>;
template<class Derived> class tcp_session_t : public detail::tcp_session_impl_t<Derived> {
public: using detail::tcp_session_impl_t<Derived>::tcp_session_impl_t;
};
class tcp_session : public tcp_session_t<tcp_session> {
public: using tcp_session_t::tcp_session_t;
};
}
#if defined(ARKNET_INCLUDE_RATE_LIMIT)
#include <arknet/tcp/tcp_stream.hpp>
namespace arknet {
struct tcp_rate_session_args : tcp_session_args { using socket_t = tcp_stream<simple_rate_policy>; };
template<class Derived> class tcp_rate_session_t : public tcp_session_impl_t<Derived, tcp_rate_session_args> {
public: using tcp_session_impl_t<Derived, tcp_rate_session_args>::tcp_session_impl_t;
};
class tcp_rate_session : public tcp_rate_session_t<tcp_rate_session> {
public: using tcp_rate_session_t::tcp_rate_session_t;
};
}
#endif
