// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/util.hpp>
#include <arknet/base/detail/function_traits.hpp>
#include <arknet/base/impl/post_cp.hpp>
namespace arknet::detail
{
struct user_timer_handle
{
    template <class T>
        requires(!std::is_same_v<std::remove_cvref_t<T>, user_timer_handle>)
    user_timer_handle(T&& value)
    {
        bind(std::forward<T>(value));
    }
    user_timer_handle(const user_timer_handle&) = default;
    user_timer_handle(user_timer_handle&&) = default;
    user_timer_handle& operator=(const user_timer_handle&) = default;
    user_timer_handle& operator=(user_timer_handle&&) = default;
    template <class T> void operator=(T&& value) { bind(std::forward<T>(value)); }
    bool operator==(const user_timer_handle&) const = default;
    template <class T> void bind(T&& value)
    {
        using Type = std::remove_cvref_t<T>;
        if constexpr (std::is_integral_v<Type>)
            handle = std::to_string(value);
        else if constexpr (std::is_floating_point_v<Type>)
            handle.assign(reinterpret_cast<const char*>(std::addressof(value)), sizeof(Type));
        else if constexpr (is_string_v<Type> || is_string_view_v<Type>)
            handle.assign(reinterpret_cast<const char*>(value.data()),
                          value.size() * sizeof(typename Type::value_type));
        else if constexpr (is_char_pointer_v<Type> || is_char_array_v<Type>)
        {
            using Character = typename char_type<T>::type;
            const auto count = value ? std::char_traits<Character>::length(value) : 0;
            handle.assign(reinterpret_cast<const char*>(value), count * sizeof(Character));
        }
        else if constexpr (std::is_pointer_v<Type>)
            handle = std::to_string(reinterpret_cast<std::uintptr_t>(value));
        else
            static_assert(always_false_v<T>, "Timer keys require a scalar, pointer, string or string view");
    }
    std::string handle;
};
struct user_timer_handle_hash
{
    std::size_t operator()(const user_timer_handle& key) const noexcept { return std::hash<std::string>{}(key.handle); }
};
struct user_timer_handle_equal
{
    bool operator()(const user_timer_handle& left, const user_timer_handle& right) const noexcept
    {
        return left == right;
    }
};
struct user_timer_obj
{
    user_timer_obj(user_timer_handle key, asio::any_io_executor executor) : id(std::move(key)), timer(executor) {}
    user_timer_handle id;
    asio::steady_timer timer;
    std::function<void()> callback;
    asio::steady_timer::duration interval{};
    std::size_t repeat = std::numeric_limits<std::size_t>::max();
    bool exited = false;
};
template <class Derived, class Args = void> class user_timer_cp
{
public:
    using user_timer_map = std::unordered_map<user_timer_handle, std::shared_ptr<user_timer_obj>,
                                              user_timer_handle_hash, user_timer_handle_equal>;

private:
    struct operation_state
    {
        std::atomic_bool alive{true};
        std::shared_ptr<std::atomic_size_t> pending = std::make_shared<std::atomic_size_t>(0);
        user_timer_map timers;
    };
    std::shared_ptr<operation_state> timers_ = std::make_shared<operation_state>();

public:
    ~user_timer_cp() { timers_->alive.store(false, std::memory_order_release); }
    template <class Key, class Milliseconds, class Function, class... Bound>
        requires(std::is_integral_v<Milliseconds> && is_callable_v<Function>)
    void start_timer(Key&& key, Milliseconds interval, Function&& function, Bound&&... bound)
    {
        start_timer(std::forward<Key>(key), std::chrono::milliseconds(interval), std::forward<Function>(function),
                    std::forward<Bound>(bound)...);
    }
    template <class Key, class Milliseconds, class Repeat, class Function, class... Bound>
        requires(std::is_integral_v<Milliseconds> && std::is_integral_v<Repeat> && is_callable_v<Function>)
    void start_timer(Key&& key, Milliseconds interval, Repeat repeat, Function&& function, Bound&&... bound)
    {
        start_timer(std::forward<Key>(key), std::chrono::milliseconds(interval), repeat,
                    std::forward<Function>(function), std::forward<Bound>(bound)...);
    }
    template <class Key, class Rep, class Period, class Function, class... Bound>
        requires is_callable_v<Function>
    void start_timer(Key&& key, std::chrono::duration<Rep, Period> interval, Function&& function, Bound&&... bound)
    {
        start_timer(std::forward<Key>(key), interval, std::size_t(-1), interval, std::forward<Function>(function),
                    std::forward<Bound>(bound)...);
    }
    template <class Key, class Rep, class Period, class Repeat, class Function, class... Bound>
        requires(std::is_integral_v<Repeat> && is_callable_v<Function>)
    void start_timer(Key&& key, std::chrono::duration<Rep, Period> interval, Repeat repeat, Function&& function,
                     Bound&&... bound)
    {
        start_timer(std::forward<Key>(key), interval, repeat, interval, std::forward<Function>(function),
                    std::forward<Bound>(bound)...);
    }
    template <class Key, class R1, class P1, class R2, class P2, class Function, class... Bound>
        requires is_callable_v<Function>
    void start_timer(Key&& key, std::chrono::duration<R1, P1> interval, std::chrono::duration<R2, P2> first,
                     Function&& function, Bound&&... bound)
    {
        start_timer(std::forward<Key>(key), interval, std::size_t(-1), first, std::forward<Function>(function),
                    std::forward<Bound>(bound)...);
    }
    template <class Key, class R1, class P1, class Repeat, class R2, class P2, class Function, class... Bound>
        requires(std::is_integral_v<Repeat> && is_callable_v<Function>)
    void start_timer(Key&& key, std::chrono::duration<R1, P1> interval, Repeat repeat,
                     std::chrono::duration<R2, P2> first, Function&& function, Bound&&... bound)
    {
        if (!repeat)
        {
            set_last_error(asio::error::invalid_argument);
            return;
        }
        auto& object = static_cast<Derived&>(*this);
        object.post(
            [state = timers_, lane = object.io_, owner = object.selfptr(),
             key = user_timer_handle(std::forward<Key>(key)), period = to_steady_duration(interval),
             first = to_steady_duration(first), count = std::size_t(repeat),
             callback = std::bind_front(std::forward<Function>(function), std::forward<Bound>(bound)...)]() mutable
            {
                cancel(state, key);
                auto timer = std::make_shared<user_timer_obj>(key, lane->executor());
                timer->interval = period;
                timer->repeat = count;
                timer->callback = std::move(callback);
                state->timers.insert_or_assign(std::move(key), timer);
                lane->timers().insert(&timer->timer);
                arm(state, lane, std::move(owner), std::move(timer), first);
            });
    }
    template <class Key> void stop_timer(Key&& key)
    {
        auto& object = static_cast<Derived&>(*this);
        asio::post(object.io_->executor(),
                   [state = timers_, owner = object.selfptr(), pending = pending_operation(timers_->pending),
                    key = user_timer_handle(std::forward<Key>(key))] { cancel(state, key); });
    }
    void stop_all_timers() { cancel_all<false>(); }
    template <class Key> bool is_timer_exists(Key&& key)
    {
        return query([state = timers_, key = user_timer_handle(std::forward<Key>(key))]
                     { return state->timers.contains(key); });
    }
    template <class Key> auto get_timer_interval(Key&& key)
    {
        return query(
            [state = timers_, key = user_timer_handle(std::forward<Key>(key))]
            {
                const auto found = state->timers.find(key);
                return found == state->timers.end() ? asio::steady_timer::duration{} : found->second->interval;
            });
    }
    template <class Key, class Rep, class Period>
    void set_timer_interval(Key&& key, std::chrono::duration<Rep, Period> duration)
    {
        static_cast<Derived&>(*this).dispatch(
            [state = timers_, key = user_timer_handle(std::forward<Key>(key)), duration = to_steady_duration(duration)]
            {
                if (auto found = state->timers.find(key); found != state->timers.end())
                    found->second->interval = duration;
            });
    }
    template <class Key, class Integer>
        requires std::is_integral_v<Integer>
    void set_timer_interval(Key&& key, Integer milliseconds)
    {
        set_timer_interval(std::forward<Key>(key), std::chrono::milliseconds(milliseconds));
    }
    template <class Key, class Duration> void reset_timer_interval(Key&& key, Duration duration)
    {
        set_timer_interval(std::forward<Key>(key), duration);
    }

protected:
    void _dispatch_stop_all_timers() { cancel_all<true>(); }
    void _wait_user_timer_operations() { wait_pending_operations(timers_->pending); }
    template <class Rep, class Period>
    void _post_user_timers(std::shared_ptr<Derived> owner, std::shared_ptr<user_timer_obj> timer,
                           std::chrono::duration<Rep, Period> delay)
    {
        arm(timers_, static_cast<Derived&>(*this).io_, std::move(owner), std::move(timer), to_steady_duration(delay));
    }

private:
    template <bool Inline> void cancel_all()
    {
        auto& object = static_cast<Derived&>(*this);
        auto handler = [state = timers_, owner = object.selfptr(), pending = pending_operation(timers_->pending)]
        {
            for (auto& [key, timer] : state->timers)
            {
                timer->exited = true;
                cancel_timer(timer->timer);
            }
            state->timers.clear();
        };
        if constexpr (Inline)
            asio::dispatch(object.io_->executor(), std::move(handler));
        else
            asio::post(object.io_->executor(), std::move(handler));
    }
    static void arm(std::shared_ptr<operation_state> state, std::shared_ptr<io_t> lane, std::shared_ptr<Derived> owner,
                    std::shared_ptr<user_timer_obj> timer, asio::steady_timer::duration delay)
    {
        timer->timer.expires_after(delay);
        auto* operation = &timer->timer;
        operation->async_wait(
            [pending = pending_operation(state->pending), state, lane, owner = std::move(owner),
             timer = std::move(timer)](error_code error) mutable
            {
                if (timer->exited || !state->alive.load(std::memory_order_acquire))
                    error = asio::error::operation_aborted;
                set_last_error(error);
                if (state->alive.load(std::memory_order_acquire))
                {
#ifdef ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR
                    timer->callback();
#else
                    if (!error)
                        timer->callback();
#endif
                }
                if (timer->repeat != std::size_t(-1))
                    --timer->repeat;
                if (error || timer->exited || !timer->repeat || !state->alive.load(std::memory_order_acquire))
                {
                    lane->timers().erase(&timer->timer);
                    const auto found = state->timers.find(timer->id);
                    if (found != state->timers.end() && found->second == timer)
                        state->timers.erase(found);
                    timer.reset();
                }
                else
                    arm(state, lane, std::move(owner), timer, timer->interval);
            });
    }
    static void cancel(const std::shared_ptr<operation_state>& state, const user_timer_handle& key)
    {
        if (auto found = state->timers.find(key); found != state->timers.end())
        {
            found->second->exited = true;
            cancel_timer(found->second->timer);
            state->timers.erase(found);
        }
    }
    template <class Function> auto query(Function&& function)
    {
        auto& object = static_cast<Derived&>(*this);
        using Result = std::invoke_result_t<Function>;
        if (object.io_->running_in_this_thread())
            return function();
        if (object.io_->context().get_executor().running_in_this_thread())
        {
            set_last_error(asio::error::operation_not_supported);
            return Result{};
        }
        const bool stopped = object.io_->context().stopped() || [&]
        {
            if constexpr (requires { object.iopool(); })
                return !object.iopool().started();
            else
                return false;
        }();
        if (stopped)
        {
            set_last_error(asio::error::operation_aborted);
            return Result{};
        }
        return object.post(std::forward<Function>(function), asio::use_future).get();
    }
};
}
