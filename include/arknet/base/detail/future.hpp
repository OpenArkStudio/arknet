// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <exception>
#include <functional>
#include <future>
#include <type_traits>
#include <utility>

namespace arknet::detail
{
template <class Function> auto make_future_task(Function&& callback)
{
    using result_type = std::invoke_result_t<Function>;
    std::promise<result_type> promise;
    auto future = promise.get_future();
    // The future owns only the result state, so abandoning a task releases its callable.
    auto task = [callback = std::forward<Function>(callback), promise = std::move(promise)]() mutable
    {
        try
        {
            if constexpr (std::is_void_v<result_type>)
            {
                std::invoke(callback);
                promise.set_value();
            }
            else
                promise.set_value(std::invoke(callback));
        }
        catch (...)
        {
            promise.set_exception(std::current_exception());
        }
    };
    return std::make_pair(std::move(future), std::move(task));
}
}
