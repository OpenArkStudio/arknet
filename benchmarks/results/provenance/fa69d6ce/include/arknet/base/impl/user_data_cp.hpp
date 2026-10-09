// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <any>
#include <type_traits>
#include <utility>
#include <arknet/base/error.hpp>
namespace arknet::detail
{
template<class Derived, class Args = void> class user_data_cp
{
public:
    template<class T> Derived& set_user_data(T&& value) { user_data_ = std::forward<T>(value); return static_cast<Derived&>(*this); }
    template<class T> Derived& user_data(T&& value) { return set_user_data(std::forward<T>(value)); }
    template<class T> T user_data() noexcept { return get_user_data<T>(); }
    template<class T> T get_user_data() noexcept
    {
        if constexpr (std::is_pointer_v<T>)
        {
            if (auto* pointer = std::any_cast<T>(&user_data_)) return *pointer;
            return std::any_cast<std::remove_pointer_t<T>>(&user_data_);
        }
        else
        {
            if (auto* value = std::any_cast<std::remove_reference_t<T>>(&user_data_)) return *value;
            if constexpr (std::is_reference_v<T>) { static std::remove_reference_t<T> empty{}; return empty; }
            else return {};
        }
    }
    Derived& clear_user_data() noexcept { user_data_.reset(); return static_cast<Derived&>(*this); }
    std::any& user_data_any() noexcept { return user_data_; }
    const std::any& user_data_any() const noexcept { return user_data_; }
protected:
    std::any user_data_;
};
}
