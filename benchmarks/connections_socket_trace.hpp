// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <chrono>
#include <cstdint>
#include <type_traits>
#include <utility>

#if ARKNET_CONTROL_BOOST
#include <boost/asio.hpp>

#else
#include <asio.hpp>

#endif

#include "connections_trace.hpp"

namespace connection_load {

#if ARKNET_CONTROL_BOOST
namespace socket_trace_asio = boost::asio;
using socket_trace_error = boost::system::error_code;
#else
namespace socket_trace_asio = asio;
using socket_trace_error = asio::error_code;
#endif

using socket_trace_clock = std::chrono::steady_clock;

// Included in recorder admission for each of the two simultaneous directions.
inline constexpr std::size_t socket_handler_reservation = 256;

template<class Handler>
struct socket_trace_completion
{
    Handler handler;
    operation_ring* ring;
    socket_trace_clock::time_point began;
    std::uint64_t sequence;
    operation_phase phase;

    void operator()(socket_trace_error error, std::size_t bytes) {
        ring->record(phase,
            std::chrono::duration_cast<std::chrono::microseconds>(socket_trace_clock::now() - began).count(), bytes,
            sequence, error.value());
        handler(error, bytes);
    }
};

// Native-control diagnostics only. The unobserved path uses the socket directly.
template<class Stream>
class socket_trace_stream
{
public:
    using executor_type = typename Stream::executor_type;

    socket_trace_stream(Stream& stream, operation_ring& ring, socket_trace_clock::time_point began)
        : stream_(stream)
        , ring_(ring)
        , began_(began) {}

    executor_type get_executor() noexcept {
        return stream_.get_executor();
    }

    operation_ring& ring() noexcept {
        return ring_;
    }

    void read_sequence(std::uint64_t sequence) noexcept {
        read_sequence_ = sequence;
    }

    void write_sequence(std::uint64_t sequence) noexcept {
        write_sequence_ = sequence;
    }

    template<class Buffers, class Handler>
    void async_read_some(const Buffers& buffers, Handler&& handler) {
        stream_.async_read_some(
            buffers, observe(operation_phase::read_start, operation_phase::read_complete,
                         socket_trace_asio::buffer_size(buffers), read_sequence_, std::forward<Handler>(handler)));
    }

    template<class Buffers, class Handler>
    void async_write_some(const Buffers& buffers, Handler&& handler) {
        stream_.async_write_some(
            buffers, observe(operation_phase::write_start, operation_phase::write_complete,
                         socket_trace_asio::buffer_size(buffers), write_sequence_, std::forward<Handler>(handler)));
    }

private:
    template<class Handler>
    auto observe(
        operation_phase start, operation_phase complete, std::size_t bytes, std::uint64_t sequence, Handler&& handler) {
        using completion_type = socket_trace_completion<std::decay_t<Handler>>;
        static_assert(sizeof(completion_type) <= sizeof(std::decay_t<Handler>) + socket_handler_reservation);
        ring_.record(start,
            std::chrono::duration_cast<std::chrono::microseconds>(socket_trace_clock::now() - began_).count(), bytes,
            sequence);
        return completion_type{std::forward<Handler>(handler), &ring_, began_, sequence, complete};
    }

    Stream& stream_;
    operation_ring& ring_;
    socket_trace_clock::time_point began_;
    std::uint64_t read_sequence_{}, write_sequence_{};
};

} // namespace connection_load

#if ARKNET_CONTROL_BOOST
namespace boost::asio {

#else
namespace asio {

#endif

// Preserve executor, allocator, cancellation slot, immediate executor and any
// other association supported by the original Asio composed-operation handler.
template<template<class, class> class Associator, class Handler, class DefaultCandidate>
struct associator<Associator, connection_load::socket_trace_completion<Handler>, DefaultCandidate>
    : Associator<Handler, DefaultCandidate>
{
    static typename Associator<Handler, DefaultCandidate>::type get(
        const connection_load::socket_trace_completion<Handler>& value) noexcept {
        return Associator<Handler, DefaultCandidate>::get(value.handler);
    }

    static auto get(
        const connection_load::socket_trace_completion<Handler>& value, const DefaultCandidate& candidate) noexcept
        -> decltype(Associator<Handler, DefaultCandidate>::get(value.handler, candidate)) {
        return Associator<Handler, DefaultCandidate>::get(value.handler, candidate);
    }
};

} // namespace asio
