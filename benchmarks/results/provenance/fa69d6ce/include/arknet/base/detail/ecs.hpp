// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <arknet/base/detail/type_traits.hpp>
#include <arknet/base/detail/match_condition.hpp>
namespace arknet::detail
{
struct component_tag {};
struct component_dummy {};
template<class Match, class... Options> class component_t : public component_tag
{
public:
    static constexpr std::size_t componentc = sizeof...(Options);
    using components_type = std::tuple<decltype(to_shared_ptr(std::declval<Options>()))...>;
    using condition_type = condition_t<std::remove_cvref_t<Match>>;
    using condition_lowest_type = typename condition_type::type;
    template<std::size_t Index> struct components
    {
        using type = std::tuple_element_t<Index, components_type>;
        using raw_type = typename type::element_type;
    };
    template<class Condition, class... Values> explicit component_t(Condition&& condition, Values&&... values)
        : condition_(std::forward<Condition>(condition)), options_(to_shared_ptr(std::forward<Values>(values))...) {}
    component_t(component_t&&) = default;
    component_t& operator=(component_t&&) = default;
    component_t clone()
    {
        return std::apply([this](const auto&... options) { return component_t(condition_.clone(), typename std::remove_cvref_t<decltype(options)>::element_type(*options)...); }, options_);
    }
    decltype(auto) operator()() { return condition_(); }
    condition_type& get_condition() noexcept { return condition_; }
    components_type& values() noexcept { return options_; }
private:
    condition_type condition_;
    components_type options_;
};
template<class Match, class... Options> component_t(Match, Options...) -> component_t<Match, Options...>;
struct ecs_base { virtual ~ecs_base() = default; };
template<class Match> class ecs_t : public ecs_base
{
public:
    using component_type = component_dummy;
    using condition_type = condition_t<Match>;
    using condition_lowest_type = typename condition_type::type;
    ecs_t() = default;
    explicit ecs_t(condition_type condition) : condition_(std::move(condition)) {}
    template<class Value> requires (!std::is_same_v<std::remove_cvref_t<Value>, ecs_t>)
    explicit ecs_t(Value&& value) : condition_(std::forward<Value>(value)) {}
    ecs_t(ecs_t&&) = default;
    ecs_t& operator=(ecs_t&&) = default;
    ecs_t clone() { if constexpr (std::is_void_v<Match>) return {}; else return ecs_t(condition_.clone()); }
    condition_type& get_condition() noexcept { return condition_; }
    component_type& get_component() noexcept { return component_; }
private:
    condition_type condition_;
    component_type component_;
};
template<class Match> ecs_t(Match) -> ecs_t<std::remove_cvref_t<Match>>;
template<class Match, class... Options> class ecs_t<component_t<Match, Options...>> : public ecs_base
{
public:
    using component_type = component_t<Match, Options...>;
    using condition_type = typename component_type::condition_type;
    using condition_lowest_type = typename condition_type::type;
    explicit ecs_t(component_type component) : component_(std::move(component)) {}
    ecs_t(ecs_t&&) = default;
    ecs_t& operator=(ecs_t&&) = default;
    ecs_t clone() { return ecs_t(component_.clone()); }
    condition_type& get_condition() noexcept { return component_.get_condition(); }
    component_type& get_component() noexcept { return component_; }
private:
    component_type component_;
};
struct ecs_helper
{
    template<class T> static constexpr bool is_component() noexcept
    {
        if constexpr (is_template_instance_of_v<std::shared_ptr, T>) return is_component<typename std::remove_cvref_t<T>::element_type>();
        else return std::is_base_of_v<component_tag, std::remove_cvref_t<T>>;
    }
    template<class... Args> static constexpr bool args_has_match_condition() noexcept { return !(is_component<Args>() && ...); }
    template<class Default, class... Args> static auto make_ecs(Default default_match, Args... args)
    {
        if constexpr (args_has_match_condition<Args...>())
        {
            if constexpr (sizeof...(Args) == 1) return to_shared_ptr(ecs_t(std::move(args)...));
            else return to_shared_ptr(ecs_t(component_t(std::move(args)...)));
        }
        else if constexpr (sizeof...(Args) == 0) return to_shared_ptr(ecs_t(std::move(default_match)));
        else return to_shared_ptr(ecs_t(component_t(std::move(default_match), std::move(args)...)));
    }
};
}
