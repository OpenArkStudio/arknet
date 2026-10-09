// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <atomic>
#include <memory>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/listener.hpp>
#include <arknet/base/define.hpp>
#include <arknet/base/detail/object.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/buffer_wrap.hpp>
#include <arknet/base/detail/ecs.hpp>
#include <arknet/base/session_mgr.hpp>
#include <arknet/base/impl/io_context_cp.hpp>
#include <arknet/base/impl/thread_id_cp.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
#include <arknet/base/impl/user_data_cp.hpp>
#include <arknet/base/impl/user_timer_cp.hpp>
#include <arknet/base/impl/post_cp.hpp>
#include <arknet/base/impl/condition_event_cp.hpp>
namespace arknet
{
class server
{
public:
    static constexpr bool is_client() noexcept { return false; }
    static constexpr bool is_session() noexcept { return false; }
    static constexpr bool is_server() noexcept { return true; }
};
}
namespace arknet::detail
{
template<class Derived, class Session> class server_impl_t : public arknet::server, public object_t<Derived>,
    public iopool_cp<Derived>, public io_context_cp<Derived>, public thread_id_cp<Derived>,
    public event_queue_cp<Derived>, public user_data_cp<Derived>, public user_timer_cp<Derived>,
    public post_cp<Derived>, public condition_event_cp<Derived>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
public:
    using super = object_t<Derived>;
    using self = server_impl_t;
    using iopoolcp = iopool_cp<Derived>;
    using args_type = typename Session::args_type;
    using key_type = std::size_t;
    template<class Scheduler> explicit server_impl_t(Scheduler&& scheduler)
        : iopoolcp(std::forward<Scheduler>(scheduler)), io_context_cp<Derived>(iopoolcp::_get_io(0)), sessions_(this->io_, state_) {}
    bool start() noexcept { return true; }
    void stop()
    {
        this->derived().dispatch([this]
        {
            this->_dispatch_stop_all_timers();
            this->_dispatch_stop_all_timed_events();
            this->notify_all_condition_events();
            this->clear_user_data();
            ecs_.reset();
        });
    }
    void destroy() { this->io_.reset(); listener_.clear(); this->destroy_iopool(); }
    bool is_started() const noexcept { return state_.load() == state_t::started; }
    bool is_stopped() const noexcept { return state_.load() == state_t::stopped; }
    void request_stop() { if (!this->derived().is_stopped()) this->post([this] { this->derived().stop(); }); }
    bool wait_stopped()
    {
        if (this->iopool().running_in_threads() || this->io_->context().get_executor().running_in_this_thread()) { set_last_error(asio::error::operation_not_supported); return false; }
        this->derived().stop();
        return this->derived().is_stopped();
    }
    template<class Data> Derived& async_send(const Data& data) { sessions_.for_each([&](auto& session) { session->async_send(data); }); return this->derived(); }
    template<class Character, class Count> requires std::is_integral_v<Count>
    Derived& async_send(Character* data, Count count) { if (data) sessions_.for_each([&](auto& session) { session->async_send(data, count); }); return this->derived(); }
    template<class Character> requires is_char_v<Character>
    Derived& async_send(Character* data) { return async_send(data, data ? std::char_traits<std::remove_cv_t<Character>>::length(data) : 0); }
    auto& acceptor() noexcept { return this->derived().acceptor(); }
    const auto& acceptor() const noexcept { return this->derived().acceptor(); }
    std::string listen_address() const noexcept { return get_listen_address(); }
    std::string get_listen_address() const noexcept { const auto endpoint = acceptor().local_endpoint(get_last_error()); return get_last_error() ? std::string{} : endpoint.address().to_string(); }
    unsigned short listen_port() const noexcept { return get_listen_port(); }
    unsigned short get_listen_port() const noexcept { return acceptor().local_endpoint(get_last_error()).port(); }
    std::size_t session_count() const noexcept { return get_session_count(); }
    std::size_t get_session_count() const noexcept { return sessions_.size(); }
    template<class Function> Derived& foreach_session(Function&& function) { sessions_.for_each(std::forward<Function>(function)); return this->derived(); }
    template<class Key> std::shared_ptr<Session> find_session(const Key& key) { return sessions_.find(key); }
    template<class Predicate> std::shared_ptr<Session> find_session_if(Predicate&& predicate) { return sessions_.find_if(std::forward<Predicate>(predicate)); }
protected:
    auto& rallocator() noexcept { return rallocator_; }
    auto& wallocator() noexcept { return wallocator_; }
    session_mgr_t<Session>& sessions() noexcept { return sessions_; }
    listener_t& listener() noexcept { return listener_; }
    std::atomic<state_t>& state() noexcept { return state_; }
    handler_memory<std::true_type, assizer<args_type>> rallocator_;
    handler_memory<std::false_type, assizer<args_type>> wallocator_;
    listener_t listener_;
    std::atomic<state_t> state_ = state_t::stopped;
    session_mgr_t<Session> sessions_;
    std::shared_ptr<void> counter_ptr_;
    std::shared_ptr<ecs_base> ecs_;
#ifndef NDEBUG
    std::atomic<int> post_send_counter_ = 0;
    std::atomic<int> post_recv_counter_ = 0;
#endif
};
}
