// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/external/asio.hpp>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

class benchmark_runtime
{
public:
    benchmark_runtime(std::string_view model, std::size_t threads)
    {
        const auto count = model == "shared" ? 1 : threads;
        for (std::size_t i = 0; i < count; ++i)
        {
            contexts_.push_back(std::make_shared<asio::io_context>());
            guards_.push_back(asio::make_work_guard(*contexts_.back()));
        }
        try
        {
            for (std::size_t i = 0; i < threads; ++i)
                workers_.emplace_back([context = contexts_[i % count]] { context->run(); });
        }
        catch (...) { stop(); throw; }
    }
    ~benchmark_runtime() { stop(); }
    benchmark_runtime(const benchmark_runtime&) = delete;
    benchmark_runtime& operator=(const benchmark_runtime&) = delete;
    asio::io_context& context_at(std::size_t index = 0) { return *contexts_[index % contexts_.size()]; }
    std::vector<asio::io_context*> server_lanes(std::size_t clients)
    {
        std::vector<asio::io_context*> result;
        for (std::size_t i = 0; i <= clients; ++i) result.push_back(&context_at(i));
        return result;
    }
    void stop()
    {
        // Drain cross-context captures before any context can destroy its services.
        for (auto& guard : guards_) guard.reset();
        for (auto& worker : workers_) if (worker.joinable()) worker.join();
        workers_.clear();
    }
private:
    std::vector<std::shared_ptr<asio::io_context>> contexts_;
    std::vector<asio::executor_work_guard<asio::io_context::executor_type>> guards_;
    std::vector<std::thread> workers_;
};
