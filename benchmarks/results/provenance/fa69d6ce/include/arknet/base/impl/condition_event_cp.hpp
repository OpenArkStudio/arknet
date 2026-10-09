// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/object.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/util.hpp>
#include <arknet/base/impl/post_cp.hpp>
namespace arknet::detail { template<class, class> class condition_event_cp; }
namespace arknet
{
class async_event : public detail::object_t<async_event> {};
class condition_event : public detail::object_t<condition_event>
{
    template<class, class> friend class detail::condition_event_cp;
public:
    explicit condition_event(const std::shared_ptr<detail::io_t>& lane) : event_timer_io_(lane) {}
    void notify()
    {
        notified_.store(true, std::memory_order_release);
        if (auto lane = event_timer_io_.lock()) asio::dispatch(lane->executor(), [self = this->selfptr()]
        {
            if (self && self->event_timer_) detail::cancel_timer(*self->event_timer_);
        });
    }
protected:
    template<class Handler> void async_wait(Handler&& callback)
    {
        auto lane = event_timer_io_.lock();
        if (!lane) return;
        event_timer_ = std::make_unique<asio::steady_timer>(lane->executor());
        lane->timers().insert(event_timer_.get());
        event_timer_->expires_at(asio::steady_timer::time_point::max());
        event_timer_->async_wait([self = this->selfptr(), lane, callback = std::forward<Handler>(callback)](const error_code&) mutable
        {
            lane->timers().erase(self->event_timer_.get());
            callback();
        });
        if (notified_.load(std::memory_order_acquire)) detail::cancel_timer(*event_timer_);
    }
    std::weak_ptr<detail::io_t> event_timer_io_;
    std::unique_ptr<asio::steady_timer> event_timer_;
private:
    std::atomic_bool notified_ = false;
};
}
namespace arknet::detail
{
template<class Derived, class Args = void> class condition_event_cp
{
    struct operation_state
    {
        std::atomic_bool alive{true};
        std::atomic_bool accepting{true};
        std::shared_ptr<std::atomic_size_t> pending = std::make_shared<std::atomic_size_t>(0);
        std::map<condition_event*, std::shared_ptr<condition_event>> events;
    };
    std::shared_ptr<operation_state> events_ = std::make_shared<operation_state>();
public:
    ~condition_event_cp() { events_->alive.store(false, std::memory_order_release); }
    template<class Function> auto post_condition_event(Function&& callback)
    {
        auto& object = static_cast<Derived&>(*this);
        auto event = std::make_shared<condition_event>(object.io_);
        if constexpr (requires { object._accepts_async_work(); })
        {
            if (!object._accepts_async_work()) { set_last_error(asio::error::operation_aborted); return event; }
        }
        asio::dispatch(object.io_->executor(), [state = events_, owner = object.selfptr(), pending = pending_operation(events_->pending), event, callback = std::forward<Function>(callback)]() mutable
        {
            if (!state->alive.load(std::memory_order_acquire) || !state->accepting.load(std::memory_order_acquire)) return;
            state->events.emplace(event.get(), event);
            event->async_wait([pending = pending_operation(state->pending), state, owner = std::move(owner), key = event.get(), callback = std::optional<std::decay_t<Function>>(std::in_place, std::move(callback))]() mutable
            {
                state->events.erase(key);
                auto current = std::move(callback);
                callback.reset();
                if (state->alive.load(std::memory_order_acquire)) (*current)();
            });
        });
        return event;
    }
    Derived& notify_all_condition_events()
    {
        auto& object = static_cast<Derived&>(*this);
        asio::dispatch(object.io_->executor(), [state = events_, owner = object.selfptr(), pending = pending_operation(events_->pending)]
        {
            for (auto& [key, event] : state->events) event->notify();
        });
        return object;
    }
protected:
    void _set_condition_acceptance(bool accepting) { events_->accepting.store(accepting, std::memory_order_release); }
    void _wait_condition_operations() { wait_pending_operations(events_->pending); }
};
}
