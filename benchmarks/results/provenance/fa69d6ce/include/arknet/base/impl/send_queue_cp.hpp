// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <atomic>
#include <concepts>
#include <functional>
#include <utility>
#include <arknet/base/error.hpp>
#include <arknet/base/detail/buffer_wrap.hpp>
namespace arknet::detail
{
template<class Callback> concept send_completion_handler =
    std::invocable<Callback&, const error_code&, std::size_t> ||
    std::invocable<Callback&, std::size_t> || std::invocable<Callback&>;
template<class Derived, class Args> class send_queue_cp
{
public:
    Derived& set_max_send_buffer_size(std::size_t bytes) noexcept
    {
        limit_.store(bytes, std::memory_order_relaxed);
        return static_cast<Derived&>(*this);
    }
    std::size_t get_max_send_buffer_size() const noexcept { return limit_.load(std::memory_order_relaxed); }
    std::size_t get_queued_send_buffer_size() const noexcept { return reserved_.load(std::memory_order_relaxed); }
protected:
    class send_buffer_guard
    {
    public:
        send_buffer_guard(send_queue_cp& queue, std::size_t bytes) noexcept : queue_(&queue), bytes_(bytes) {}
        send_buffer_guard(send_buffer_guard&& other) noexcept : queue_(std::exchange(other.queue_, nullptr)), bytes_(other.bytes_) {}
        send_buffer_guard(const send_buffer_guard&) = delete;
        ~send_buffer_guard() { release(); }
        bool resize(std::size_t bytes) noexcept
        {
            if (!queue_ || !queue_->adjust(bytes_, bytes)) return false;
            bytes_ = bytes;
            return true;
        }
        void release() noexcept
        {
            if (auto* queue = std::exchange(queue_, nullptr))
            {
                queue->reserved_.fetch_sub(bytes_, std::memory_order_relaxed);
                queue->operations_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    private:
        send_queue_cp* queue_;
        std::size_t bytes_;
    };
    template<class Callback> static void _call_completion(Callback& callback, const error_code& error, std::size_t bytes)
    {
        if constexpr (std::invocable<Callback&, const error_code&, std::size_t>) std::invoke(callback, error, bytes);
        else if constexpr (std::invocable<Callback&, std::size_t>) std::invoke(callback, bytes);
        else std::invoke(callback);
    }
    bool _can_send_buffer_size(std::size_t bytes) const noexcept
    {
        return fits(reserved_.load(std::memory_order_relaxed), bytes) && operations_.load(std::memory_order_relaxed) < operation_limit;
    }
    bool _reserve_send_buffer(std::size_t bytes) noexcept
    {
        // Empty payloads still consume operation storage and must be bounded.
        const auto previous = operations_.fetch_add(1, std::memory_order_relaxed);
        if (previous < operation_limit && adjust(0, bytes)) return true;
        operations_.fetch_sub(1, std::memory_order_relaxed);
        return false;
    }
private:
    static constexpr std::size_t operation_limit = 1024;
    bool fits(std::size_t used, std::size_t additional) const noexcept
    {
        const auto limit = limit_.load(std::memory_order_relaxed);
        return additional <= limit && used <= limit - additional;
    }
    bool adjust(std::size_t previous, std::size_t next) noexcept
    {
        if (next <= previous)
        {
            reserved_.fetch_sub(previous - next, std::memory_order_relaxed);
            return true;
        }
        const auto additional = next - previous;
        auto used = reserved_.load(std::memory_order_relaxed);
        do
        {
            if (!fits(used, additional)) return false;
        } while (!reserved_.compare_exchange_weak(used, used + additional, std::memory_order_relaxed));
        return true;
    }
    std::atomic_size_t limit_{16 * 1024 * 1024};
    std::atomic_size_t reserved_{};
    std::atomic_size_t operations_{};
};
}
