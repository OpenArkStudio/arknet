// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <concepts>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
namespace arknet::detail
{
template <class T> struct remove_cvref : std::type_identity<std::remove_cvref_t<T>>
{
};
template <class T> using remove_cvref_t = std::remove_cvref_t<T>;
template <class Enum> constexpr auto to_underlying(Enum value) noexcept
{
    return static_cast<std::underlying_type_t<Enum>>(value);
}
template <class... T> constexpr void ignore_unused(const T&...) noexcept {}
template <class...> inline constexpr bool always_false_v = false;
template <class Tuple, class Function> void for_each_tuple(Tuple&& tuple, Function&& function)
{
    std::apply([&](auto&&... values) { (std::invoke(function, std::forward<decltype(values)>(values)), ...); },
               std::forward<Tuple>(tuple));
}
template <template <class...> class Template, class, class... Args>
    struct is_template_instantiable : std::bool_constant < requires
{
    typename Template<Args...>;
} > {};
template <template <class...> class Template, class... Args>
inline constexpr bool is_template_instantiable_v = is_template_instantiable<Template, void, Args...>::value;
template <template <class...> class Template, class T> struct is_template_instance_of : std::false_type
{
};
template <template <class...> class Template, class... Args>
struct is_template_instance_of<Template, Template<Args...>> : std::true_type
{
};
template <template <class...> class Template, class T>
inline constexpr bool is_template_instance_of_v = is_template_instance_of<Template, std::remove_cvref_t<T>>::value;
template <class T> using is_tuple = is_template_instance_of<std::tuple, std::remove_cvref_t<T>>;
template <class T, class = void>
struct is_char
    : std::bool_constant<std::same_as<std::remove_cvref_t<T>, char> || std::same_as<std::remove_cvref_t<T>, wchar_t> ||
                         std::same_as<std::remove_cvref_t<T>, char8_t> ||
                         std::same_as<std::remove_cvref_t<T>, char16_t> ||
                         std::same_as<std::remove_cvref_t<T>, char32_t>>
{
};
template <class T> inline constexpr bool is_char_v = is_char<T>::value;
template <class T, class = void> struct is_string : is_template_instance_of<std::basic_string, std::remove_cvref_t<T>>
{
};
template <class T> inline constexpr bool is_string_v = is_string<T>::value;
template <class T, class = void>
struct is_string_view : is_template_instance_of<std::basic_string_view, std::remove_cvref_t<T>>
{
};
template <class T> inline constexpr bool is_string_view_v = is_string_view<T>::value;
template <class T, class = void>
struct is_char_pointer : std::bool_constant<std::is_pointer_v<std::remove_cvref_t<T>> &&
                                            is_char_v<std::remove_pointer_t<std::remove_cvref_t<T>>>>
{
};
template <class T> inline constexpr bool is_char_pointer_v = is_char_pointer<T>::value;
template <class T, class = void>
struct is_char_array : std::bool_constant<std::is_array_v<std::remove_cvref_t<T>> &&
                                          is_char_v<std::remove_all_extents_t<std::remove_cvref_t<T>>>>
{
};
template <class T> inline constexpr bool is_char_array_v = is_char_array<T>::value;
template <class T, bool = is_char_v<T> || is_char_pointer_v<T> || is_char_array_v<T>>
struct char_type : std::type_identity<typename T::value_type>
{
};
template <class T>
struct char_type<T, true>
    : std::type_identity<std::remove_cv_t<std::remove_pointer_t<std::remove_all_extents_t<std::remove_cvref_t<T>>>>>
{
};
template <class T>
inline constexpr bool is_character_string_v =
    is_string_v<T> || is_string_view_v<T> || is_char_pointer_v<T> || is_char_array_v<T>;
template <class Stream, class Value, class = void>
    struct has_stream_operator : std::bool_constant < requires(Stream& stream, Value& value)
{
    stream << value;
} > {};
template <class Target, class Value, class = void>
struct has_equal_operator : std::bool_constant<std::is_assignable_v<Target&, Value>>
{
};
template <class T, class = void> struct has_bool_operator : std::bool_constant < requires(T& value)
{
    value.operator bool();
} > {};
template <class T, class = void>
struct can_convert_to_string
    : std::bool_constant<std::is_constructible_v<std::string, T> && std::is_assignable_v<std::string&, T>>
{
};
template <class T> inline constexpr bool can_convert_to_string_v = can_convert_to_string<std::remove_cvref_t<T>>::value;
template <class T> struct shared_ptr_adapter : std::type_identity<std::shared_ptr<std::remove_cvref_t<T>>>
{
};
template <class T> struct shared_ptr_adapter<std::shared_ptr<T>> : std::type_identity<std::shared_ptr<T>>
{
};
template <class T> auto to_shared_ptr(T&& value)
{
    if constexpr (is_template_instance_of_v<std::shared_ptr, T>)
        return std::forward<T>(value);
    else
        return std::make_shared<std::remove_cvref_t<T>>(std::forward<T>(value));
}
template <class T> struct element_type_adapter : std::type_identity<std::remove_cvref_t<T>>
{
};
template <class T> struct element_type_adapter<std::shared_ptr<T>> : std::type_identity<T>
{
};
template <class T, class Deleter> struct element_type_adapter<std::unique_ptr<T, Deleter>> : std::type_identity<T>
{
};
template <class T> struct element_type_adapter<T*> : std::type_identity<T>
{
};
template <class T> decltype(auto) to_element_ref(T& value) noexcept
{
    if constexpr (std::is_pointer_v<T> || is_template_instance_of_v<std::shared_ptr, T> ||
                  is_template_instance_of_v<std::unique_ptr, T>)
        return *value;
    else
        return (value);
}
}
namespace arknet
{
using detail::ignore_unused;
using detail::to_shared_ptr;
using detail::to_underlying;
}
