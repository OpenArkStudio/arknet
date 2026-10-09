// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/util.hpp>
namespace arknet::detail
{
class pending_operation
{
public:
    explicit pending_operation(std::shared_ptr<std::atomic_size_t> counter) : counter_(std::move(counter)) { counter_->fetch_add(1, std::memory_order_relaxed); }
    pending_operation(pending_operation&& other) noexcept : counter_(std::move(other.counter_)) {}
    pending_operation(const pending_operation&) = delete;
    ~pending_operation()
    {
        if (counter_) { counter_->fetch_sub(1, std::memory_order_release); counter_->notify_all(); }
    }
private:
    std::shared_ptr<std::atomic_size_t> counter_;
};
inline void wait_pending_operations(const std::shared_ptr<std::atomic_size_t>& counter)
{
    for (auto count = counter->load(std::memory_order_acquire); count; count = counter->load(std::memory_order_acquire)) counter->wait(count, std::memory_order_acquire);
}
template<class Derived, class Args = void> class post_cp
{
    struct operation_state
    {
        std::atomic_bool alive{true};
        std::atomic_bool accepting{true};
        std::shared_ptr<std::atomic_size_t> pending = std::make_shared<std::atomic_size_t>(0);
        handler_memory<std::false_type, assizer<Args>> allocator;
        std::set<asio::steady_timer*> timers;
    };
    std::shared_ptr<operation_state> tasks_ = std::make_shared<operation_state>();
public:
    ~post_cp() { tasks_->alive.store(false, std::memory_order_release); }
    template<class Function> Derived& post(Function&& callback) { return submit<false>(std::forward<Function>(callback)); }
    template<class Function> Derived& dispatch(Function&& callback) { return submit<true>(std::forward<Function>(callback)); }
    template<class Function, class Rep, class Period> Derived& post(Function&& callback, std::chrono::duration<Rep, Period> delay)
    {
        auto& object = static_cast<Derived&>(*this);
        post([state = tasks_, lane = object.io_, owner = object.selfptr(), callback = std::forward<Function>(callback), delay]() mutable
        {
            auto timer = std::make_unique<asio::steady_timer>(lane->executor());
            state->timers.insert(timer.get());
            lane->timers().insert(timer.get());
            timer->expires_after(to_steady_duration(delay));
            auto* operation = timer.get();
            operation->async_wait([state, pending = pending_operation(state->pending), owner = std::move(owner), lane, timer = std::move(timer), callback = std::optional<std::decay_t<Function>>(std::in_place, std::move(callback))](const error_code& error) mutable
            {
                lane->timers().erase(timer.get());
                state->timers.erase(timer.get());
                set_last_error(error);
                auto current = std::move(callback);
                callback.reset();
                if (!state->alive.load(std::memory_order_acquire)) return;
#ifdef ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR
                (*current)();
#else
                if (!error) (*current)();
#endif
            });
        });
        return object;
    }
    template<class Function, class Allocator> auto post(Function&& callback, asio::use_future_t<Allocator>) { return submit_future<false>(std::forward<Function>(callback)); }
    template<class Function, class Allocator> auto dispatch(Function&& callback, asio::use_future_t<Allocator>) { return submit_future<true>(std::forward<Function>(callback)); }
    template<class Function, class Rep, class Period, class Allocator> auto post(Function&& callback, std::chrono::duration<Rep, Period> delay, asio::use_future_t<Allocator>)
    {
        std::packaged_task<std::invoke_result_t<Function>()> task(std::forward<Function>(callback));
        auto future = task.get_future();
        post([task = std::move(task)]() mutable { task(); }, delay);
        return future;
    }
    Derived& stop_all_timed_events() { return cancel_tasks<false>(); }
    Derived& stop_all_timed_tasks() { return stop_all_timed_events(); }
protected:
    Derived& _dispatch_stop_all_timed_events()
    {
        return cancel_tasks<true>();
    }
    void _set_post_acceptance(bool accepting) { tasks_->accepting.store(accepting, std::memory_order_release); }
    void _wait_post_operations() { wait_pending_operations(tasks_->pending); }
private:
    template<bool Inline> Derived& cancel_tasks()
    {
        auto& object = static_cast<Derived&>(*this);
        auto handler = make_allocator(tasks_->allocator, [state = tasks_, owner = object.selfptr(), pending = pending_operation(tasks_->pending)]
        {
            for (auto* timer : state->timers) cancel_timer(*timer);
        });
        if constexpr (Inline) asio::dispatch(object.io_->executor(), std::move(handler));
        else asio::post(object.io_->executor(), std::move(handler));
        return object;
    }
    template<bool Inline, class Function> Derived& submit(Function&& callback)
    {
        auto& object = static_cast<Derived&>(*this);
        if constexpr (requires { object._accepts_async_work(); })
        {
            if (!object._accepts_async_work()) { set_last_error(asio::error::operation_aborted); return object; }
        }
        auto handler = make_allocator(tasks_->allocator, [state = tasks_, owner = object.selfptr(), pending = pending_operation(tasks_->pending), callback = std::optional<std::decay_t<Function>>(std::in_place, std::forward<Function>(callback))]() mutable
        {
            auto current = std::move(callback);
            callback.reset();
            if (state->alive.load(std::memory_order_acquire) && state->accepting.load(std::memory_order_acquire)) (*current)();
        });
        if constexpr (Inline) asio::dispatch(object.io_->executor(), std::move(handler));
        else asio::post(object.io_->executor(), std::move(handler));
        return object;
    }
    template<bool Inline, class Function> auto submit_future(Function&& callback)
    {
        std::packaged_task<std::invoke_result_t<Function>()> task(std::forward<Function>(callback));
        auto future = task.get_future();
        submit<Inline>([task = std::move(task)]() mutable { task(); });
        return future;
    }
};
}
