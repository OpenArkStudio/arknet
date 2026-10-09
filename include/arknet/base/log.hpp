// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <arknet/external/assert.hpp>

#if defined(ARKNET_ENABLE_LOG) && __has_include(<spdlog/spdlog.h>)
#include <spdlog/spdlog.h>
#define ARKNET_LOG(...) spdlog::log(__VA_ARGS__)
#define ARKNET_LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
#define ARKNET_LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define ARKNET_LOG_INFOR(...) spdlog::info(__VA_ARGS__)
#define ARKNET_LOG_WARNS(...) spdlog::warn(__VA_ARGS__)
#define ARKNET_LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define ARKNET_LOG_FATAL(...) spdlog::critical(__VA_ARGS__)
#else
#define ARKNET_LOG(...) ((void)0)
#define ARKNET_LOG_TRACE(...) ((void)0)
#define ARKNET_LOG_DEBUG(...) ((void)0)
#define ARKNET_LOG_INFOR(...) ((void)0)
#define ARKNET_LOG_WARNS(...) ((void)0)
#define ARKNET_LOG_ERROR(...) ((void)0)
#define ARKNET_LOG_FATAL(...) ((void)0)
#endif

namespace arknet::detail
{
inline bool& has_unexpected_behavior() noexcept
{
    thread_local bool detected = false;
    return detected;
}
}
