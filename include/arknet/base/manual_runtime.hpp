// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <thread>

#include <arknet/external/asio.hpp>

namespace arknet {

// A caller-driven execution context. It creates no worker to run user handlers:
// poll() dispatches them on the constructing thread. Asio platform services may
// still use internal helper threads, such as Windows timers or DNS resolution.
class manual_runtime
{
    struct state
    {
        const std::thread::id owner = std::this_thread::get_id();
        asio::io_context context{1};
        asio::executor_work_guard<asio::io_context::executor_type> work{context.get_executor()};
        bool polling{};
    };

    std::shared_ptr<state> state_ = std::make_shared<state>();

public:
    using executor_type = asio::io_context::executor_type;

    manual_runtime() = default;
    manual_runtime(const manual_runtime&) = delete;
    manual_runtime& operator=(const manual_runtime&) = delete;
    manual_runtime(manual_runtime&&) = delete;
    manual_runtime& operator=(manual_runtime&&) = delete;

    // Standard Asio executors do not keep their execution context alive.
    // Finish external operations and stop concurrent submissions before the
    // runtime is destroyed. Destruction is not a drain or join barrier.
    executor_type get_executor() noexcept {
        return state_->context.get_executor();
    }

    // Executes at most max_handlers ready Asio handlers without waiting.
    // Exceptions propagate to the caller; a later poll() remains valid.
    std::size_t poll(std::size_t max_handlers = 64) {
        // A handler may release the façade. Keep its execution context and
        // polling flag alive until this invocation has completely returned.
        auto runtime = state_;
        if (std::this_thread::get_id() != runtime->owner)
        {
            throw std::logic_error("manual_runtime::poll requires its constructing thread");
        }
        if (runtime->polling)
        {
            throw std::logic_error("manual_runtime::poll does not permit reentrant calls");
        }

        struct poll_guard
        {
            bool& active;

            explicit poll_guard(bool& flag) noexcept
                : active(flag) {
                active = true;
            }

            ~poll_guard() {
                active = false;
            }
        } guard(runtime->polling);

        std::size_t completed{};
        while (completed < max_handlers)
        {
            if (!runtime->context.poll_one())
            {
                break;
            }
            ++completed;
        }
        return completed;
    }
};

} // namespace arknet
