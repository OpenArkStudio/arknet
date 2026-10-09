// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <concepts>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace arknet
{

template <class T> std::string to_string(T&& value)
{
    using type = std::remove_cvref_t<T>;
    if constexpr (std::is_pointer_v<type> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<type>>, char>)
    {
        return value ? std::string(value) : std::string{};
    }
    else if constexpr (std::is_same_v<type, char>)
    {
        return std::string(1, value);
    }
    else if constexpr (std::constructible_from<std::string, T>)
    {
        return std::string(std::forward<T>(value));
    }
    else if constexpr (std::is_arithmetic_v<type>)
    {
        return std::to_string(value);
    }
    else
    {
        std::ostringstream stream;
        stream << std::forward<T>(value);
        return stream.str();
    }
}

template <class T> std::string_view to_string_view(const T& value)
{
    using type = std::remove_cvref_t<T>;
    if constexpr (std::is_pointer_v<type> && std::is_same_v<std::remove_cv_t<std::remove_pointer_t<type>>, char>)
    {
        return value ? std::string_view(value) : std::string_view{};
    }
    else if constexpr (std::is_same_v<type, char>)
    {
        return std::string_view(&value, 1);
    }
    else
    {
        return std::string_view(value);
    }
}

}
