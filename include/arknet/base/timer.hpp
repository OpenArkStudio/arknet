// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <memory>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/define.hpp>
#include <arknet/base/detail/object.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/impl/thread_id_cp.hpp>
#include <arknet/base/impl/user_timer_cp.hpp>
#include <arknet/base/impl/post_cp.hpp>
#include <arknet/base/impl/condition_event_cp.hpp>
namespace arknet::detail
{
struct template_args_timer
{
    static constexpr std::size_t allocator_storage_size = 256;
};
template <class Derived, class Args = template_args_timer>
class timer_impl_t : public object_t<Derived>,
                     public iopool_cp<Derived, Args>,
                     public thread_id_cp<Derived, Args>,
                     public user_timer_cp<Derived, Args>,
                     public post_cp<Derived, Args>,
                     public condition_event_cp<Derived, Args>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;

public:
    using super = object_t<Derived>;
    using self = timer_impl_t;
    using iopoolcp = iopool_cp<Derived, Args>;
    using args_type = Args;
    timer_impl_t() : iopoolcp(1), io_(iopoolcp::_get_io(0)) { start(); }
    template <class Scheduler>
        requires(!std::is_integral_v<std::remove_cvref_t<Scheduler>>)
    explicit timer_impl_t(Scheduler&& scheduler)
        : iopoolcp(std::forward<Scheduler>(scheduler)), io_(iopoolcp::_get_io(0))
    {
        start();
    }
    ~timer_impl_t() { stop(); }
    bool start()
    {
        if (!this->start_iopool())
            return false;
        accepting_.store(true, std::memory_order_release);
        this->_set_post_acceptance(true);
        this->_set_condition_acceptance(true);
        io_->regobj(&this->derived());
        this->dispatch([lane = io_] { lane->init_thread_id(); });
        return true;
    }
    void stop()
    {
        if (!io_)
            return;
        accepting_.store(false, std::memory_order_release);
        this->_set_post_acceptance(false);
        this->_set_condition_acceptance(false);
        if (!this->is_iopool_stopped())
        {
            io_->unregobj(&this->derived());
            this->_dispatch_stop_all_timers();
            this->_dispatch_stop_all_timed_events();
            this->notify_all_condition_events();
            this->stop_iopool();
        }
        if (io_->context().get_executor().running_in_this_thread() || io_->context().stopped())
            return;
        // A strand barrier can run before canceled waits; drain their actual completions.
        this->_wait_post_operations();
        this->_wait_user_timer_operations();
        this->_wait_condition_operations();
        this->_wait_post_operations();
    }
    void destroy()
    {
        stop();
        io_.reset();
        this->destroy_iopool();
    }
    io_t& io() noexcept { return *io_; }
    const io_t& io() const noexcept { return *io_; }

protected:
    bool _accepts_async_work() const noexcept { return accepting_.load(std::memory_order_acquire); }
    std::atomic_bool accepting_{false};
    auto& rallocator() noexcept { return wallocator_; }
    auto& wallocator() noexcept { return wallocator_; }
    std::shared_ptr<io_t> io_;
    handler_memory<std::false_type, assizer<Args>> wallocator_;
};
}
namespace arknet
{
class timer : public detail::timer_impl_t<timer>
{
public:
    using detail::timer_impl_t<timer>::timer_impl_t;
};
}
