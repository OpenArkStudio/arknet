// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>
namespace arknet::detail
{
template<class Signature = void> struct lambda_dummy_t {};
template<class Return, class... Args> struct lambda_dummy_t<Return(Args...)>
{
    Return operator()(const Args&...)
    {
        if constexpr (std::is_reference_v<Return>) { static std::remove_reference_t<Return> value{}; return value; }
        else if constexpr (!std::is_void_v<Return>) return {};
    }
};
template<class T, class Signature = void> struct function_traits { static constexpr bool is_callable = false; };
template<class Return, class... Args> struct function_traits<Return(Args...)>
{
    static constexpr bool is_callable = true;
    static constexpr std::size_t argc = sizeof...(Args);
    using function_type = Return(Args...);
    using return_type = Return;
    using stl_function_type = std::function<function_type>;
    using stl_lambda_type = lambda_dummy_t<function_type>;
    using pointer = Return(*)(Args...);
    using class_type = void;
    using tuple_type = std::tuple<Args...>;
    using pod_tuple_type = std::tuple<std::remove_cvref_t<Args>...>;
    template<std::size_t Index> struct args : std::type_identity<std::tuple_element_t<Index, tuple_type>> {};
};
template<class Class, class Return, class... Args> struct function_traits<Class, Return(Args...)> : function_traits<Return(Args...)> { using class_type = Class; };
template<class Return, class... Args> struct function_traits<Return(*)(Args...)> : function_traits<Return(Args...)> {};
template<class Return, class... Args> struct function_traits<Return(*)(Args...) noexcept> : function_traits<Return(Args...)> {};
template<class Return, class... Args> struct function_traits<Return(&)(Args...)> : function_traits<Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...)> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) &> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) & noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) &&> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) && noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const &> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const & noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const &&> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const && noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) volatile> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) volatile noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) volatile &> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) volatile & noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) volatile &&> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) volatile && noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const volatile> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const volatile noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const volatile &> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const volatile & noexcept> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const volatile &&> : function_traits<Class, Return(Args...)> {};
template<class Class, class Return, class... Args> struct function_traits<Return(Class::*)(Args...) const volatile && noexcept> : function_traits<Class, Return(Args...)> {};
template<class Callable> requires requires { &Callable::operator(); }
struct function_traits<Callable, void> : function_traits<decltype(&Callable::operator())> {};
template<class T> inline constexpr bool is_callable_v = function_traits<std::decay_t<T>>::is_callable;
template<class Callable, class... Args> inline constexpr bool is_template_callable_v = std::is_invocable_v<Callable&, Args...>;
template<class Callable, class Void, class... Args> struct is_template_callable : std::bool_constant<is_template_callable_v<Callable, Args...>> {};
template<class Callable> auto to_function(Callable&& callable) { return typename function_traits<std::decay_t<Callable>>::stl_function_type(std::forward<Callable>(callable)); }
template<class Callable> auto to_function_pointer(const Callable& callable) noexcept { return static_cast<typename function_traits<Callable>::pointer>(callable); }
}
