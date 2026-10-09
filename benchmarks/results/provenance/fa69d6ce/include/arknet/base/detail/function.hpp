// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#ifndef ARKNET_DETAIL_FUNCTION_HPP
#define ARKNET_DETAIL_FUNCTION_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace arknet::detail
{
    template<class Args>
    struct function_size_traits
    {
        static constexpr std::size_t value = 0;
    };

    template<class Signature, std::size_t StorageSize = 0>
    class function;

    // C++20 std::function owns a copyable handle to a potentially move-only handler.
    template<class R, class... Args, std::size_t StorageSize>
    class function<R(Args...), StorageSize>
    {
    public:
        using signature = R(Args...);

        function() noexcept = default;
        function(function&&) noexcept = default;
        function& operator=(function&&) noexcept = default;
        function(const function&) = delete;
        function& operator=(const function&) = delete;

        template<class F>
            requires (!std::is_same_v<std::remove_cvref_t<F>, function>)
        function(F&& handler)
            : handler_([owned = std::make_shared<std::decay_t<F>>(std::forward<F>(handler))]
                (Args... args) -> R { return std::invoke(*owned, std::forward<Args>(args)...); })
        {
        }

        template<class F>
            requires (!std::is_same_v<std::remove_cvref_t<F>, function>)
        function& operator=(F&& handler)
        {
            function replacement(std::forward<F>(handler));
            swap(replacement);
            return *this;
        }

        explicit operator bool() const noexcept { return static_cast<bool>(handler_); }
        R operator()(Args... args) const
        {
            // A queued handler can remove its own function while it runs.
            auto active = handler_;
            return active(std::forward<Args>(args)...);
        }
        void reset() noexcept { handler_ = nullptr; }
        void swap(function& other) noexcept { handler_.swap(other.handler_); }

    private:
        std::function<R(Args...)> handler_;
    };
}

#endif
