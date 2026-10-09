// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <arknet/base/detail/util.hpp>
#include <arknet/base/error.hpp>
#include <arknet/base/log.hpp>
namespace arknet::detail
{
enum class event_type : std::int8_t { recv, send, connect, disconnect, accept, handshake, upgrade, init, start, stop, max };
template<class = void> constexpr std::string_view to_string(event_type event)
{
    constexpr std::array<std::string_view, 11> names{"recv", "send", "connect", "disconnect", "accept", "handshake", "upgrade", "init", "start", "stop", "max"};
    const auto index = static_cast<std::size_t>(event);
    return index < names.size() ? names[index] : "none";
}
struct observer_base { virtual ~observer_base() = default; };
template<class... Args> class observer_t : public observer_base
{
public:
    using func_type = std::function<void(Args...)>;
    using args_type = std::tuple<Args...>;
    observer_t(const observer_t& other) : fn_(other.fn_ ? std::make_shared<func_type>(*other.fn_) : nullptr) {}
    observer_t(observer_t&&) = default;
    template<class Function, class... Bound> requires (!std::is_same_v<std::remove_cvref_t<Function>, observer_t>)
    explicit observer_t(Function&& function, Bound&&... bound) { bind(std::forward<Function>(function), std::forward<Bound>(bound)...); }
    template<class Function, class... Bound> void bind(Function&& function, Bound&&... bound)
    {
        fn_ = std::make_shared<func_type>(std::bind_front(std::forward<Function>(function), std::forward<Bound>(bound)...));
    }
    template<class Function, class... Bound> void bind_memfn(Function&& function, Bound&&... bound) { bind(std::forward<Function>(function), std::forward<Bound>(bound)...); }
    template<class Function, class... Bound> void bind_memfn_front(Function&& function, Bound&&... bound) { bind(std::forward<Function>(function), std::forward<Bound>(bound)...); }
    template<class Function, class... Bound> void bind_fn_front(Function&& function, Bound&&... bound) { bind(std::forward<Function>(function), std::forward<Bound>(bound)...); }
    void operator()(Args... args)
    {
        // Retain the same callback instance while it replaces or clears its listener.
        auto current = fn_;
        if (current && *current) (*current)(std::forward<Args>(args)...);
    }
    func_type move() noexcept
    {
        auto current = std::exchange(fn_, {});
        return current ? std::move(*current) : func_type{};
    }
private:
    std::shared_ptr<func_type> fn_;
};
class listener_t
{
public:
    template<class Observer> void bind(event_type event, Observer&& observer)
    {
        find(event) = std::make_unique<std::remove_cvref_t<Observer>>(std::forward<Observer>(observer));
    }
    template<class... Args> void notify(event_type event, Args&&... args)
    {
        using Observer = observer_t<std::conditional_t<std::is_same_v<std::remove_cvref_t<Args>, std::string_view>, std::string_view, Args>...>;
        if (auto* observer = static_cast<Observer*>(find(event).get()))
        {
            try { (*observer)(std::forward<Args>(args)...); }
            catch (const std::exception& exception) { ARKNET_LOG_ERROR("Callback {} failed: {}", to_string(event), exception.what()); ARKNET_ASSERT(false); }
            catch (...) { ARKNET_LOG_ERROR("Callback {} failed", to_string(event)); ARKNET_ASSERT(false); }
        }
    }
    std::unique_ptr<observer_base>& find(event_type event) noexcept { return observers_[static_cast<std::size_t>(event)]; }
    const std::unique_ptr<observer_base>& find(event_type event) const noexcept { return observers_[static_cast<std::size_t>(event)]; }
    void clear() noexcept { for (auto& observer : observers_) observer.reset(); }
private:
    std::array<std::unique_ptr<observer_base>, static_cast<std::size_t>(event_type::max)> observers_;
};
}
