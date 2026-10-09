// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <arknet/base/error.hpp>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <concepts>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace arknet::detail
{
using io_context_work_guard = asio::executor_work_guard<asio::io_context::executor_type>;

class io_t
{
    struct lane_state
    {
        explicit lane_state(std::shared_ptr<asio::io_context> context, bool serialize)
            : context(std::move(context))
        {
            if (!this->context) throw std::invalid_argument("null IO context");
            if (serialize) strand.emplace(this->context->get_executor());
            executor = strand ? asio::any_io_executor(*strand) : this->context->get_executor();
        }
        std::shared_ptr<asio::io_context> context;
        std::optional<asio::strand<asio::io_context::executor_type>> strand;
        asio::any_io_executor executor;
        std::atomic<std::size_t> pending{};
        std::unordered_set<asio::steady_timer*> timers;
        std::unordered_map<const void*, std::function<void()>> owners;
        std::mutex identity_mutex;
        std::thread::id worker;
    };
    std::shared_ptr<lane_state> lane_;

public:
    explicit io_t(std::shared_ptr<asio::io_context> context, bool serialize = true)
        : lane_(std::make_shared<lane_state>(std::move(context), serialize)) {}
    io_t(const io_t&) = delete;
    io_t& operator=(const io_t&) = delete;

    asio::io_context& context() noexcept { return *lane_->context; }
    const asio::io_context& context() const noexcept { return *lane_->context; }
    asio::any_io_executor executor() const noexcept { return lane_->executor; }
    std::shared_ptr<asio::io_context> context_ptr() const noexcept { return lane_->context; }
    std::atomic<std::size_t>& pending() noexcept { return lane_->pending; }
    const std::atomic<std::size_t>& pending() const noexcept { return lane_->pending; }
    auto& timers() noexcept { return lane_->timers; }
    const auto& timers() const noexcept { return lane_->timers; }

    bool running_in_this_thread() const noexcept
    {
        return lane_->strand ? lane_->strand->running_in_this_thread()
                             : lane_->context->get_executor().running_in_this_thread();
    }
    void init_thread_id() noexcept
    {
        std::lock_guard lock(lane_->identity_mutex);
        lane_->worker = std::this_thread::get_id();
    }
    void fini_thread_id() noexcept
    {
        std::lock_guard lock(lane_->identity_mutex);
        lane_->worker = {};
    }
    std::thread::id get_thread_id() const noexcept
    {
        if (lane_->context->get_executor().running_in_this_thread()) return std::this_thread::get_id();
        std::lock_guard lock(lane_->identity_mutex);
        return lane_->worker;
    }

    template<class Object>
    void regobj(Object* object)
    {
        if (!object) return;
        asio::dispatch(executor(), [lane = lane_, object, owner = object->derived().selfptr()]() mutable
        {
            auto work = asio::make_work_guard(*lane->context);
            lane->owners.insert_or_assign(object,
                [object, owner = std::move(owner), work = std::move(work)] { object->stop(); });
        });
    }
    template<class Object>
    void unregobj(Object* object)
    {
        if (!object) return;
        asio::post(executor(), [lane = lane_, object, owner = object->derived().selfptr()]
        {
            lane->owners.erase(object);
        });
    }
    void cancel()
    {
        asio::post(executor(), [lane = lane_]
        {
            for (auto* timer : lane->timers)
            {
                try { timer->cancel(); }
                catch (const system_error&) {}
            }
            lane->timers.clear();
            // Detach owners before callbacks can unregister or stop another owner.
            auto owners = std::move(lane->owners);
            lane->owners.clear();
            for (auto& [key, stop] : owners) stop();
        });
    }
};

class iopool_base
{
public:
    virtual ~iopool_base() = default;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool started() const noexcept = 0;
    virtual bool stopped() const noexcept { return !started(); }
    virtual void destroy() noexcept = 0;
    virtual std::shared_ptr<io_t> get(std::size_t index) noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual bool running_in_threads() const noexcept = 0;
    virtual bool owns_context() const noexcept = 0;
};

class io_pool : public iopool_base
{
    enum class phase { idle, running, draining };
    std::vector<std::shared_ptr<io_t>> lanes_;
    std::vector<io_context_work_guard> work_;
    std::vector<std::thread> workers_;
    mutable std::mutex lifecycle_mutex_;
    std::condition_variable lifecycle_changed_;
    phase phase_ = phase::idle;
    std::atomic<std::size_t> cursor_{};

    void drain_until_stopped(const std::shared_ptr<io_t>& target)
    {
        std::unique_lock lock(lifecycle_mutex_);
        while (!target->context().stopped())
        {
            lock.unlock();
            cancel();
            lock.lock();
            lifecycle_changed_.wait_for(lock, std::chrono::milliseconds(2));
        }
    }

    template<class F, class... A>
    auto submit(std::size_t index, F&& function, A&&... arguments)
    {
        using result_type = std::invoke_result_t<F, A...>;
        std::packaged_task<result_type()> task(
            [function = std::forward<F>(function), arguments = std::make_tuple(std::forward<A>(arguments)...)]() mutable
            -> result_type { return std::apply(std::move(function), std::move(arguments)); });
        auto result = task.get_future();
        std::lock_guard lock(lifecycle_mutex_);
        if (phase_ != phase::running)
        {
            std::promise<result_type> rejected;
            result = rejected.get_future();
            rejected.set_exception(std::make_exception_ptr(system_error(asio::error::operation_aborted)));
            return result;
        }
        auto lane = get(index);
        ++lane->pending();
        try
        {
            asio::post(lane->executor(), [lane, task = std::move(task)]() mutable
            {
                task();
                --lane->pending();
                lane->pending().notify_all();
            });
        }
        catch (...)
        {
            --lane->pending();
            lane->pending().notify_all();
            throw;
        }
        return result;
    }

public:
    explicit io_pool(std::size_t count = 0)
    {
        if (!count) count = (std::max)(1u, std::thread::hardware_concurrency());
        lanes_.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
            lanes_.push_back(std::make_shared<io_t>(std::make_shared<asio::io_context>(1), false));
    }
    io_pool(const io_pool&) = delete;
    io_pool& operator=(const io_pool&) = delete;
    ~io_pool() override
    {
        if (running_in_threads()) std::terminate();
        stop();
    }

    bool start() override
    {
        clear_last_error();
        std::unique_lock lock(lifecycle_mutex_);
        if (phase_ == phase::running) return true;
        if (phase_ != phase::idle || lanes_.empty())
        {
            set_last_error(asio::error::operation_aborted);
            return false;
        }
        std::latch ready(lanes_.size());
        try
        {
            for (auto& lane : lanes_)
            {
                lane->context().restart();
                work_.push_back(asio::make_work_guard(lane->context()));
                workers_.emplace_back([this, lane, &ready]
                {
                    lane->init_thread_id();
                    ready.count_down();
                    lane->context().run();
                    lane->fini_thread_id();
                #if defined(ARKNET_ENABLE_SSL)
                    OPENSSL_thread_stop();
                #endif
                    lifecycle_changed_.notify_all();
                });
            }
            ready.wait();
            phase_ = phase::running;
            return true;
        }
        catch (...)
        {
            for (auto& lane : lanes_) lane->context().stop();
            for (auto& worker : workers_) worker.join();
            workers_.clear();
            work_.clear();
            throw;
        }
    }
    void stop() override
    {
        if (running_in_threads()) { cancel(); return; }
        {
            std::unique_lock lock(lifecycle_mutex_);
            lifecycle_changed_.wait(lock, [&] { return phase_ != phase::draining; });
            if (phase_ == phase::idle) return;
            phase_ = phase::draining;
        }
        cancel();
        for (auto& lane : lanes_)
            while (lane->pending().load()) std::this_thread::yield();
        // The listener remains runnable until its registered sessions release it.
        work_.front().reset();
        drain_until_stopped(lanes_.front());
        for (auto& guard : work_) guard.reset();
        for (auto& lane : lanes_) drain_until_stopped(lane);
        for (auto& worker : workers_) worker.join();
        {
            std::lock_guard lock(lifecycle_mutex_);
            workers_.clear();
            work_.clear();
            phase_ = phase::idle;
        }
        lifecycle_changed_.notify_all();
    }
    bool started() const noexcept override
    {
        std::lock_guard lock(lifecycle_mutex_);
        return phase_ == phase::running;
    }
    bool stopped() const noexcept override
    {
        std::lock_guard lock(lifecycle_mutex_);
        return phase_ == phase::idle;
    }
    std::size_t size() const noexcept override { return lanes_.size(); }
    std::size_t next(std::size_t index = std::size_t(-1)) noexcept
    {
        return (index == std::size_t(-1) ? cursor_.fetch_add(1) : index) % size();
    }
    std::shared_ptr<io_t> get(std::size_t index = std::size_t(-1)) noexcept override
    {
        return lanes_[next(index)];
    }
    asio::io_context& get_context(std::size_t index = std::size_t(-1)) noexcept { return get(index)->context(); }
    std::shared_ptr<asio::io_context> get_context_ptr(std::size_t index = std::size_t(-1)) noexcept
    {
        return get(index)->context_ptr();
    }
    bool running_in_threads() const noexcept override
    {
        return std::ranges::any_of(lanes_, [](const auto& lane)
        {
            return lane->context().get_executor().running_in_this_thread();
        });
    }
    bool running_in_thread(std::size_t index) const noexcept
    {
        return index < size() && lanes_[index]->running_in_this_thread();
    }
    std::thread::id get_thread_id(std::size_t index) const noexcept { return lanes_[index % size()]->get_thread_id(); }
    std::thread::native_handle_type get_thread_handle(std::size_t index)
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (workers_.empty()) throw std::logic_error("IO pool is stopped");
        return workers_[index % workers_.size()].native_handle();
    }
    bool owns_context() const noexcept override { return true; }
    void cancel()
    {
        for (auto& lane : lanes_) if (!lane->context().stopped()) lane->cancel();
    }
    void wait_for_io_context_stopped() { stop(); }
    void destroy() noexcept override
    {
        stop();
        lanes_.clear();
    }
    template<class F, class... A>
    requires std::invocable<F, A...>
    auto post(F&& function, A&&... arguments)
    {
        auto least_busy = std::ranges::min_element(lanes_, {}, [](const auto& lane) { return lane->pending().load(); });
        return submit(static_cast<std::size_t>(least_busy - lanes_.begin()),
            std::forward<F>(function), std::forward<A>(arguments)...);
    }
    template<std::integral I, class F, class... A>
    auto post(I index, F&& function, A&&... arguments)
    {
        return submit(static_cast<std::size_t>(index), std::forward<F>(function), std::forward<A>(arguments)...);
    }
    template<class Rep, class Period>
    void wait_for(std::chrono::duration<Rep, Period> duration)
    {
        if (running_in_threads()) { set_last_error(asio::error::operation_not_supported); return; }
        clear_last_error();
        std::this_thread::sleep_for(duration);
    }
    template<class Clock, class Duration>
    void wait_until(std::chrono::time_point<Clock, Duration> deadline) { wait_for(deadline - Clock::now()); }
    template<std::integral... I>
    int wait_signal(I... numbers)
    {
        if (running_in_threads()) { set_last_error(asio::error::operation_not_supported); return 0; }
        asio::io_context local;
        asio::signal_set signals(local);
        (signals.add(numbers), ...);
        int delivered = 0;
        signals.async_wait([&](error_code ec, int number) { set_last_error(ec); delivered = number; });
        local.run();
        return delivered;
    }
};
using iopool = io_pool;
using default_iopool = io_pool;

class user_iopool final : public iopool_base
{
    std::vector<std::shared_ptr<io_t>> lanes_;
    std::atomic<bool> active_{};
    std::atomic<std::size_t> cursor_{};
    std::mutex stop_mutex_;
public:
    explicit user_iopool(std::vector<std::shared_ptr<io_t>> lanes) : lanes_(std::move(lanes))
    {
        if (lanes_.empty()) throw std::invalid_argument("empty IO scheduler");
    }
    bool start() override { clear_last_error(); active_ = true; return true; }
    void stop() override
    {
        if (running_in_threads()) return;
        std::lock_guard lock(stop_mutex_);
        if (!active_.exchange(false)) return;
        for (auto& lane : lanes_)
        {
            while (lane->pending().load()) std::this_thread::yield();
            if (lane->context().stopped()) continue;
            std::promise<void> barrier;
            auto ready = barrier.get_future();
            asio::post(lane->executor(), [barrier = std::move(barrier)]() mutable { barrier.set_value(); });
            ready.get();
        }
    }
    bool started() const noexcept override { return active_.load(); }
    std::size_t size() const noexcept override { return lanes_.size(); }
    std::size_t next(std::size_t index) noexcept
    {
        return (index == std::size_t(-1) ? cursor_.fetch_add(1) : index) % size();
    }
    std::shared_ptr<io_t> get(std::size_t index) noexcept override { return lanes_[next(index)]; }
    bool running_in_threads() const noexcept override
    {
        return std::ranges::any_of(lanes_, [](const auto& lane)
        {
            return lane->context().get_executor().running_in_this_thread();
        });
    }
    bool owns_context() const noexcept override { return false; }
    void destroy() noexcept override { stop(); lanes_.clear(); }
};

template<class Derived, class Args = void>
class iopool_cp
{
    std::mutex wait_mutex_;
    std::condition_variable wait_changed_;
    bool stop_requested_ = false;
    std::atomic<std::size_t> cursor_{};

    template<class T>
    static std::vector<std::shared_ptr<io_t>> to_iots(T&& scheduler)
    {
        using type = std::remove_cvref_t<T>;
        if constexpr (std::derived_from<type, iopool_base>)
        {
            std::vector<std::shared_ptr<io_t>> lanes;
            for (std::size_t index = 0; index < scheduler.size(); ++index) lanes.push_back(scheduler.get(index));
            return lanes;
        }
        else if constexpr (std::same_as<type, std::shared_ptr<asio::io_context>>)
            return {std::make_shared<io_t>(scheduler, true)};
        else if constexpr (std::same_as<type, asio::io_context*>)
            return {std::make_shared<io_t>(std::shared_ptr<asio::io_context>(scheduler, [](auto*) {}), true)};
        else if constexpr (std::same_as<type, asio::io_context>)
        {
            static_assert(std::is_lvalue_reference_v<T>, "external IO contexts must be lvalues");
            return to_iots(std::addressof(scheduler));
        }
        else if constexpr (std::same_as<type, std::shared_ptr<io_t>>)
        {
            if (!scheduler) throw std::invalid_argument("null IO lane");
            return {scheduler};
        }
        else if constexpr (std::same_as<type, io_t*>)
        {
            if (!scheduler) throw std::invalid_argument("null IO lane");
            return {std::shared_ptr<io_t>(scheduler, [](auto*) {})};
        }
        else if constexpr (std::same_as<type, io_t>)
        {
            static_assert(std::is_lvalue_reference_v<T>, "external IO lanes must be lvalues");
            return to_iots(std::addressof(scheduler));
        }
        else if constexpr (std::ranges::range<type>)
        {
            std::vector<std::shared_ptr<io_t>> lanes;
            for (auto&& item : scheduler)
                for (auto& lane : to_iots(item)) lanes.push_back(std::move(lane));
            return lanes;
        }
        else static_assert(sizeof(T) == 0, "unsupported IO scheduler");
    }

protected:
    std::unique_ptr<iopool_base> iopool_;
    std::vector<std::shared_ptr<io_t>> iots_;

    std::shared_ptr<io_t> _get_io(std::size_t index = std::size_t(-1)) noexcept
    {
        return iots_[(index == std::size_t(-1) ? cursor_.fetch_add(1) : index) % iots_.size()];
    }
    bool is_iopool_started() const noexcept { return iopool_->started(); }
    bool is_iopool_stopped() const noexcept { return iopool_->stopped(); }
    bool is_external_iopool() const noexcept { return !iopool_->owns_context(); }
    bool start_iopool()
    {
        { std::lock_guard lock(wait_mutex_); stop_requested_ = false; }
        if (!iopool_->start()) return false;
        for (auto& lane : iots_) asio::dispatch(lane->executor(), [lane] { lane->init_thread_id(); });
        return true;
    }
    void stop_iopool()
    {
        { std::lock_guard lock(wait_mutex_); stop_requested_ = true; }
        wait_changed_.notify_all();
        iopool_->stop();
    }
    void destroy_iopool() noexcept
    {
        iopool_->destroy();
        iots_.clear();
    }

public:
    template<class T>
    explicit iopool_cp(T&& scheduler)
    {
        if constexpr (std::integral<std::remove_cvref_t<T>>)
            iopool_ = std::make_unique<io_pool>(static_cast<std::size_t>(scheduler));
        else iopool_ = std::make_unique<user_iopool>(to_iots(std::forward<T>(scheduler)));
        for (std::size_t index = 0; index < iopool_->size(); ++index) iots_.push_back(iopool_->get(index));
    }
    iopool_base& iopool() noexcept { return *iopool_; }
    const iopool_base& iopool() const noexcept { return *iopool_; }
    void wait_stop()
    {
        if (iopool_->running_in_threads()) { set_last_error(asio::error::operation_not_supported); return; }
        clear_last_error();
        std::unique_lock lock(wait_mutex_);
        wait_changed_.wait(lock, [&] { return stop_requested_; });
    }
    template<class Rep, class Period>
    void wait_for(std::chrono::duration<Rep, Period> duration)
    {
        if (iopool_->running_in_threads()) { set_last_error(asio::error::operation_not_supported); return; }
        clear_last_error();
        std::this_thread::sleep_for(duration);
    }
    template<class Clock, class Duration>
    void wait_until(std::chrono::time_point<Clock, Duration> deadline) { wait_for(deadline - Clock::now()); }
    template<std::integral... I>
    int wait_signal(I... numbers)
    {
        if (iopool_->running_in_threads()) { set_last_error(asio::error::operation_not_supported); return 0; }
        asio::io_context local;
        asio::signal_set signals(local);
        (signals.add(numbers), ...);
        int delivered = 0;
        signals.async_wait([&](error_code ec, int number) { set_last_error(ec); delivered = number; });
        local.run();
        return delivered;
    }
};
}

namespace arknet
{
using io_t = detail::io_t;
using io_pool = detail::io_pool;
using iopool = io_pool;
}
