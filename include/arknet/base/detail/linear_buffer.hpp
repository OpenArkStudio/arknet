// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
#include <arknet/external/asio.hpp>
namespace arknet
{
template <class Container> class basic_linear_buffer
{
public:
    using allocator_type = typename Container::allocator_type;
    using size_type = typename Container::size_type;
    using const_buffers_type = asio::const_buffer;
    using mutable_buffers_type = asio::mutable_buffer;
    basic_linear_buffer() = default;
    explicit basic_linear_buffer(size_type maximum) noexcept : limit_(maximum) {}
    size_type size() const noexcept { return committed_ - consumed_; }
    size_type max_size() const noexcept { return limit_; }
    size_type capacity() const noexcept { return bytes_.capacity(); }
    const_buffers_type data() const noexcept { return {bytes_.empty() ? nullptr : bytes_.data() + consumed_, size()}; }
    mutable_buffers_type prepare(size_type count)
    {
        const auto available = size();
        if (count > limit_ - available)
            throw std::length_error("arknet buffer limit exceeded");
        if (consumed_ && count > bytes_.capacity() - committed_)
        {
            std::memmove(bytes_.data(), bytes_.data() + consumed_, available);
            consumed_ = 0;
            committed_ = available;
        }
        bytes_.resize(committed_ + count);
        return {bytes_.empty() ? nullptr : bytes_.data() + committed_, count};
    }
    void commit(size_type count) noexcept { committed_ += std::min(count, bytes_.size() - committed_); }
    void consume(size_type count) noexcept
    {
        consumed_ += std::min(count, size());
        if (consumed_ == committed_)
        {
            consumed_ = committed_ = 0;
            bytes_.clear();
        }
    }
    void shrink_to_fit()
    {
        const auto remaining = size();
        if (consumed_)
            std::memmove(bytes_.data(), bytes_.data() + consumed_, remaining);
        consumed_ = 0;
        committed_ = remaining;
        bytes_.resize(remaining);
        bytes_.shrink_to_fit();
    }

private:
    Container bytes_;
    size_type consumed_ = 0;
    size_type committed_ = 0;
    size_type limit_ = std::numeric_limits<size_type>::max();
};
using linear_buffer = basic_linear_buffer<std::vector<char>>;
}
