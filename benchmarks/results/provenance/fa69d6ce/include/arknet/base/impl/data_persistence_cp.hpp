// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <algorithm>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/detail/util.hpp>
namespace arknet::detail
{
template<class T> struct is_span : std::false_type {};
template<class T, std::size_t Extent> struct is_span<std::span<T, Extent>> : std::true_type {};
template<class Derived, class Args> class data_persistence_cp
{
    static std::string copy_bytes(asio::const_buffer bytes)
    {
        if (!bytes.size()) return {};
        return {static_cast<const char*>(bytes.data()), bytes.size()};
    }
protected:
    template<class Data> static auto _own_send_data(Data&& data)
    {
        using Type = std::remove_cvref_t<Data>;
        if constexpr (is_string_view_v<Type>)
        {
            using String = std::basic_string<typename Type::value_type>;
            return data.empty() ? String{} : String(data.begin(), data.end());
        }
        else if constexpr (is_char_array_v<Type>)
        {
            using Character = typename char_type<Type>::type;
            const auto view = std::span(data);
            return std::basic_string<Character>(view.begin(), std::find(view.begin(), view.end(), Character{}));
        }
        else if constexpr (is_char_pointer_v<Type>)
        {
            using Character = typename char_type<Type>::type;
            return data ? std::basic_string<Character>(data) : std::basic_string<Character>{};
        }
        else if constexpr (is_span<Type>::value)
        {
            return copy_bytes(asio::const_buffer(data.data(), data.size_bytes()));
        }
        else if constexpr (std::is_convertible_v<Type, asio::const_buffer> || std::is_array_v<Type>)
        {
            return copy_bytes(asio::buffer(data));
        }
        else return std::forward<Data>(data);
    }
    template<class Data> auto _data_persistence(Data&& data)
    {
        auto source = _own_send_data(std::forward<Data>(data));
        auto transformed = call_data_filter_before_send(static_cast<Derived&>(*this), std::move(source));
        // A filter can return a view into source, which must be copied while source lives.
        return _own_send_data(std::move(transformed));
    }
    template<class Character, class Count> auto _data_persistence(Character* data, Count count)
    {
        using Type = std::remove_cv_t<Character>;
        return _data_persistence(data ? std::basic_string_view<Type>(data, static_cast<std::size_t>(count)) : std::basic_string_view<Type>{});
    }
};
}
