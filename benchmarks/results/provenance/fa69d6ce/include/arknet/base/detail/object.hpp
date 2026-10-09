// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <memory>
#include <type_traits>
#include <arknet/base/error.hpp>
namespace arknet { class object {}; }
namespace arknet::detail
{
template<class Derived, bool Shared = true>
class object_t : public arknet::object,
                 public std::conditional_t<Shared, std::enable_shared_from_this<Derived>,
                                            std::type_identity<Derived>>
{
protected:
    Derived& derived() noexcept { return static_cast<Derived&>(*this); }
    const Derived& derived() const noexcept { return static_cast<const Derived&>(*this); }
    std::shared_ptr<Derived> selfptr() noexcept
    {
        if constexpr (Shared) return this->weak_from_this().lock();
        else return {};
    }
};
}
