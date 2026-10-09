// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <future>
#include <memory>
#include <queue>
#include <utility>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/function.hpp>
#include <arknet/base/detail/future.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/util.hpp>
namespace arknet::detail
{
template <class, class> class event_queue_cp;
template <class...> class defer_event;
template <class Derived> class event_queue_guard
{
    template <class, class> friend class event_queue_cp;
    template <class...> friend class defer_event;

public:
    event_queue_guard() noexcept = default;
    event_queue_guard(std::nullptr_t) noexcept {}
    event_queue_guard(event_queue_guard&& other) noexcept
        : derive(other.derive), derive_ptr_(std::move(other.derive_ptr_)), valid_(std::exchange(other.valid_, false))
    {
    }
    event_queue_guard(const event_queue_guard&) = delete;
    event_queue_guard& operator=(const event_queue_guard&) = delete;
    ~event_queue_guard() noexcept
    {
        if (valid_)
            derive->next_event(std::move(*this));
    }
    bool empty() const noexcept { return !valid_; }
    bool is_empty() const noexcept { return empty(); }

private:
    explicit event_queue_guard(Derived& object) : derive(&object), derive_ptr_(object.selfptr()), valid_(true) {}
    Derived* derive = nullptr;
    std::shared_ptr<Derived> derive_ptr_;
    bool valid_ = false;
};
template <class Function> class defer_event<Function>
{
public:
    template <class Callable> explicit defer_event(Callable&& callable) : function_(std::forward<Callable>(callable)) {}
    defer_event(defer_event&& other) noexcept
        : function_(std::move(other.function_)), armed_(std::exchange(other.armed_, false))
    {
    }
    defer_event(const defer_event&) = delete;
    ~defer_event()
    {
        if (armed_)
            function_();
    }
    bool empty() const noexcept { return !armed_; }
    bool is_empty() const noexcept { return empty(); }
    bool is_event_queue_guard_empty() const noexcept { return true; }

private:
    Function function_;
    bool armed_ = true;
};
template <> class defer_event<void>
{
public:
    defer_event() = default;
    defer_event(std::nullptr_t) {}
    bool empty() const noexcept { return true; }
    bool is_empty() const noexcept { return true; }
    bool is_event_queue_guard_empty() const noexcept { return true; }
};
template <class Derived> struct defer_eqg_dummy
{
    void operator()(event_queue_guard<Derived>) {}
};
template <class Function, class Derived> class defer_event<Function, Derived, std::false_type>
{
    template <class...> friend class defer_event;

public:
    template <class Callable>
    defer_event(Callable&& callable, std::nullptr_t) : function_(std::forward<Callable>(callable))
    {
    }
    defer_event(defer_event&& other) noexcept
        : function_(std::move(other.function_)), armed_(std::exchange(other.armed_, false))
    {
    }
    defer_event(const defer_event&) = delete;
    ~defer_event()
    {
        if (armed_)
            function_(event_queue_guard<Derived>{});
    }
    bool empty() const noexcept { return !armed_; }
    bool is_empty() const noexcept { return empty(); }
    bool is_event_queue_guard_empty() const noexcept { return true; }
    auto move_event() noexcept { return std::move(*this); }
    event_queue_guard<Derived> move_guard() noexcept { return {}; }

private:
    Function function_;
    bool armed_ = true;
};
template <class Function, class Derived> class defer_event<Function, Derived, std::true_type>
{
public:
    template <class Callable>
    defer_event(Callable&& callable, event_queue_guard<Derived> guard)
        : function_(std::forward<Callable>(callable)), guard_(std::move(guard))
    {
    }
    defer_event(defer_event<Function, Derived, std::false_type> event, event_queue_guard<Derived> guard)
        : function_(std::move(event.function_)), guard_(std::move(guard)), armed_(std::exchange(event.armed_, false))
    {
    }
    defer_event(defer_event&& other) noexcept
        : function_(std::move(other.function_)), guard_(std::move(other.guard_)),
          armed_(std::exchange(other.armed_, false))
    {
    }
    defer_event(const defer_event&) = delete;
    ~defer_event()
    {
        if (armed_)
            function_(std::move(guard_));
    }
    bool empty() const noexcept { return !armed_; }
    bool is_empty() const noexcept { return empty(); }
    bool is_event_queue_guard_empty() const noexcept { return guard_.empty(); }
    auto move_event() noexcept
    {
        defer_event<Function, Derived, std::false_type> result(std::move(function_), nullptr);
        result.armed_ = std::exchange(armed_, false);
        return result;
    }
    auto move_guard() noexcept { return std::move(guard_); }

private:
    Function function_;
    event_queue_guard<Derived> guard_;
    bool armed_ = true;
};
template <class Derived> class defer_event<void, Derived>
{
public:
    defer_event() = default;
    explicit defer_event(event_queue_guard<Derived> guard) : guard_(std::move(guard)) {}
    defer_event(defer_event&&) = default;
    bool empty() const noexcept { return true; }
    bool is_empty() const noexcept { return true; }
    bool is_event_queue_guard_empty() const noexcept { return guard_.empty(); }
    auto move_event() noexcept
    {
        defer_event<defer_eqg_dummy<Derived>, Derived, std::false_type> result(defer_eqg_dummy<Derived>{}, nullptr);
        result.armed_ = false;
        return result;
    }
    auto move_guard() noexcept { return std::move(guard_); }

private:
    event_queue_guard<Derived> guard_;
};
template <class Function> defer_event(Function) -> defer_event<Function>;
defer_event(std::nullptr_t) -> defer_event<void>;
template <class Function, class Derived>
defer_event(Function, event_queue_guard<Derived>) -> defer_event<Function, Derived, std::true_type>;
template <class Function, class Derived>
defer_event(defer_event<Function, Derived, std::false_type>,
            event_queue_guard<Derived>) -> defer_event<Function, Derived, std::true_type>;
template <class Derived> defer_event(event_queue_guard<Derived>) -> defer_event<void, Derived>;
template <class Derived, class Args = void> class event_queue_cp
{
public:
    std::size_t get_pending_event_count() const noexcept
    {
        if (!static_cast<const Derived&>(*this).io_->running_in_this_thread())
        {
            set_last_error(asio::error::operation_not_supported);
            return 0;
        }
        return events_.size();
    }
    template <class Callable> Derived& post_queued_event(Callable&& callable)
    {
        std::packaged_task<std::invoke_result_t<Callable>()> task(std::forward<Callable>(callable));
        return post_event([task = std::move(task)](event_queue_guard<Derived>) mutable { task(); });
    }
    template <class Callable, class Allocator>
    auto post_queued_event(Callable&& callable, asio::use_future_t<Allocator>)
    {
        auto [future, task] = make_future_task(std::forward<Callable>(callable));
        post_event([task = std::move(task)](event_queue_guard<Derived>) mutable { task(); });
        return std::move(future);
    }

protected:
    template <class Callable> Derived& push_event(Callable&& callable)
    {
        auto& object = static_cast<Derived&>(*this);
#ifndef ARKNET_STRONG_EVENT_ORDER
        if (object.io_->running_in_this_thread())
        {
            enqueue(std::forward<Callable>(callable));
            return object;
        }
#endif
        return post_event(std::forward<Callable>(callable));
    }
    template <class Callable> Derived& post_event(Callable&& callable)
    {
        auto& object = static_cast<Derived&>(*this);
        asio::post(object.io_->executor(),
                   make_allocator(object.wallocator(), [this, owner = object.selfptr(),
                                                        callback = std::forward<Callable>(callable)]() mutable
                                  { enqueue(std::move(callback)); }));
        return object;
    }
    template <class Callable> Derived& disp_event(Callable&& callable, event_queue_guard<Derived> guard)
    {
        auto& object = static_cast<Derived&>(*this);
        if (guard.empty())
            return push_event(std::forward<Callable>(callable));
        asio::dispatch(object.io_->executor(),
                       make_allocator(object.wallocator(),
                                      [callback = std::forward<Callable>(callable), guard = std::move(guard)]() mutable
                                      { callback(std::move(guard)); }));
        return object;
    }
    template <class = void> Derived& next_event(event_queue_guard<Derived> guard)
    {
        auto& object = static_cast<Derived&>(*this);
        if (!object.io_->running_in_this_thread())
        {
            asio::post(object.io_->executor(),
                       [this, guard = std::move(guard)]() mutable { next_event(std::move(guard)); });
            return object;
        }
        guard.valid_ = false;
        ARKNET_ASSERT(!events_.empty());
        events_.pop();
        advance_pending_ = true;
        pump();
        return object;
    }
    std::queue<function<void(event_queue_guard<Derived>), function_size_traits<Args>::value>> events_;

private:
    template <class Callable> void enqueue(Callable&& callable)
    {
        const bool first = events_.empty();
        events_.emplace(std::forward<Callable>(callable));
        if (first)
        {
            advance_pending_ = true;
            pump();
        }
    }
    void pump()
    {
        if (pumping_)
            return;
        pumping_ = true;
        // A synchronous completion requests another iteration rather than recursing.
        while (std::exchange(advance_pending_, false) && !events_.empty())
            events_.front()(event_queue_guard<Derived>(static_cast<Derived&>(*this)));
        pumping_ = false;
    }
    bool pumping_ = false;
    bool advance_pending_ = false;
};
}
