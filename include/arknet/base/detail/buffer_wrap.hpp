// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <algorithm>
#include <functional>
#include <limits>
#include <string_view>
#include <type_traits>
#include <arknet/external/asio.hpp>
namespace arknet::detail
{
inline constexpr std::size_t min_size = 512;
template <class T, class = void> struct buffer_has_limit : std::bool_constant<std::is_constructible_v<T, std::size_t>>
{
};
template <class T, class = void> struct buffer_has_max_size : std::bool_constant < requires(const T& buffer)
{
    buffer.max_size();
} > {};
struct callback_helper
{
    template <class Callback> static void call(Callback& callback, std::size_t count)
    {
        if constexpr (std::is_invocable_v<Callback&, std::size_t>)
            std::invoke(callback, count);
        else
            std::invoke(callback);
    }
};
struct empty_buffer
{
    using size_type = std::size_t;
    constexpr size_type size() const noexcept { return 0; }
    constexpr size_type max_size() const noexcept { return 0; }
    constexpr size_type capacity() const noexcept { return 0; }
    asio::const_buffer data() const noexcept { return {}; }
    asio::mutable_buffer prepare(size_type) noexcept { return {}; }
    void commit(size_type) noexcept {}
    void consume(size_type) noexcept {}
};
template <class Buffer> struct proxy_buffer
{
    using size_type = std::size_t;
    proxy_buffer() = default;
    explicit proxy_buffer(size_type) {}
    auto size() const { return b_->size(); }
    auto max_size() const { return b_->max_size(); }
    auto capacity() const { return b_->capacity(); }
    auto data() const { return b_->data(); }
    auto prepare(size_type count) { return b_->prepare(count); }
    void commit(size_type count) { b_->commit(count); }
    void consume(size_type count) { b_->consume(count); }
    void bind_buffer(Buffer* buffer) { b_ = buffer; }
    Buffer* b_ = nullptr;
};
template <class T> struct buffer_identity : std::type_identity<T>
{
};
template <class T> struct buffer_identity<proxy_buffer<T>> : std::type_identity<T>
{
};
}
namespace arknet
{
template <class Buffer, bool Limited = detail::buffer_has_limit<Buffer>::value> class buffer_wrap : public Buffer
{
public:
    using buffer_type = typename detail::buffer_identity<Buffer>::type;
    using size_type = std::size_t;
    using Buffer::Buffer;
    buffer_wrap() = default;
    explicit buffer_wrap(size_type maximum)
        requires Limited
        : Buffer(maximum), max_(maximum)
    {
    }
    explicit buffer_wrap(size_type maximum)
        requires(!Limited)
        : Buffer(), max_(maximum)
    {
    }
    buffer_wrap(size_type initial, size_type maximum)
        requires Limited
        : Buffer(maximum), pre_(initial), max_(maximum)
    {
        if constexpr (!requires { this->b_; })
            Buffer::prepare(std::min(initial, maximum));
    }
    buffer_wrap(size_type initial, size_type maximum)
        requires(!Limited)
        : Buffer(), pre_(initial), max_(maximum)
    {
        if constexpr (!requires { this->b_; })
            Buffer::prepare(std::min(initial, maximum));
    }
    buffer_type& base() noexcept
    {
        if constexpr (requires { this->b_; })
            return *this->b_;
        else
            return *this;
    }
    const buffer_type& base() const noexcept
    {
        if constexpr (requires { this->b_; })
            return *this->b_;
        else
            return *this;
    }
    size_type pre_size() const noexcept
    {
        if constexpr (std::is_same_v<Buffer, detail::empty_buffer>)
            return 0;
        else
            return pre_;
    }
    size_type max_size() const noexcept
    {
        if constexpr (detail::buffer_has_max_size<Buffer>::value)
            return Buffer::max_size();
        else
            return max_;
    }
    buffer_wrap& pre_size(size_type size) noexcept
    {
        pre_ = size;
        return *this;
    }
    buffer_wrap& max_size(size_type size) noexcept
    {
        if constexpr (!Limited)
            max_ = size;
        return *this;
    }
    std::string_view data_view() noexcept
    {
        auto bytes = this->data();
        return bytes.size() ? std::string_view(static_cast<const char*>(bytes.data()), bytes.size())
                            : std::string_view{};
    }

private:
    size_type pre_ = detail::min_size;
    size_type max_ = std::numeric_limits<size_type>::max();
};
}
