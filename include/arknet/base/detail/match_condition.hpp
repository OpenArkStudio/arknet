// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <arknet/external/asio.hpp>
namespace arknet::detail
{
using iterator = asio::buffers_iterator<asio::streambuf::const_buffers_type>;
using diff_type = iterator::difference_type;
inline std::pair<iterator, bool> dgram_match_role(iterator first, iterator last) noexcept
{
    if (first == last)
        return {first, false};
    const auto marker = static_cast<unsigned char>(*first);
    const auto prefix = marker < 254 ? 1 : marker == 254 ? 3 : 9;
    if (last - first < prefix)
        return {first, false};
    std::uint64_t length = marker;
    if (prefix != 1)
    {
        length = 0;
        for (int index = 1; index < prefix; ++index)
            length |= std::uint64_t(static_cast<unsigned char>(first[index])) << (8 * (index - 1));
        const auto minimum = prefix == 3 ? 254u : 65536u;
        if (length < minimum || length > std::uint64_t(std::numeric_limits<diff_type>::max()))
            return {first, true};
    }
    const auto payload = first + prefix;
    if (std::uint64_t(last - payload) < length)
        return {first, false};
    return {payload + static_cast<diff_type>(length), true};
}
struct use_dgram_t
{
};
struct hook_buffer_t
{
};
template <class T> class condition_t
{
public:
    using type = T;
    template <class Value>
        requires(!std::is_same_v<std::remove_cvref_t<Value>, condition_t>)
    explicit condition_t(Value&& value) : value_(std::forward<Value>(value))
    {
    }
    condition_t(condition_t&&) = default;
    condition_t& operator=(condition_t&&) = default;
    condition_t(const condition_t&) = delete;
    condition_t& operator=(const condition_t&) = delete;
    condition_t clone() { return condition_t(value_); }
    decltype(auto) operator()() noexcept { return lowest(); }
    decltype(auto) lowest() noexcept
    {
        if constexpr (std::is_same_v<T, use_dgram_t>)
            return &dgram_match_role;
        else if constexpr (std::is_same_v<T, hook_buffer_t>)
            return asio::transfer_at_least(1);
        else
            return (value_);
    }

private:
    T value_;
};
template <class T> condition_t(T) -> condition_t<std::remove_cvref_t<T>>;
template <> class condition_t<void>
{
public:
    using type = void;
    condition_t clone() const noexcept { return {}; }
    void operator()() const noexcept {}
    void lowest() const noexcept {}
};
}
namespace arknet
{
inline constexpr detail::use_dgram_t use_dgram{};
inline constexpr detail::hook_buffer_t hook_buffer{};
}
