// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <atomic>
#include <concepts>
#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/define.hpp>
#include <arknet/base/detail/buffer_wrap.hpp>
#include <arknet/base/impl/data_persistence_cp.hpp>
#include <arknet/base/impl/send_queue_cp.hpp>
namespace arknet::detail
{
ARKNET_CLASS_FORWARD_DECLARE_BASE;
template <class Derived, class Args>
class send_cp : public data_persistence_cp<Derived, Args>, public send_queue_cp<Derived, Args>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    using queue_type = send_queue_cp<Derived, Args>;
    using queue_type::_call_completion;
    using typename queue_type::send_buffer_guard;
    using result_type = std::pair<error_code, std::size_t>;

public:
    template <class Data> bool async_send(Data&& data)
    {
        return async_send(std::forward<Data>(data), ignore_completion{});
    }
    template <class Character>
        requires is_char_v<Character>
    bool async_send(Character* data)
    {
        return async_send(data, string_length(data));
    }
    template <class Element, std::integral Count> bool async_send(Element* data, Count count)
    {
        return async_send(data, count, ignore_completion{});
    }
    template <class Data> auto async_send(Data&& data, asio::use_future_t<>)
    {
        return future_for([&](auto callback) { async_send(std::forward<Data>(data), std::move(callback)); });
    }
    template <class Character>
        requires is_char_v<Character>
    auto async_send(Character* data, asio::use_future_t<> token)
    {
        return async_send(data, string_length(data), token);
    }
    template <class Element, std::integral Count> auto async_send(Element* data, Count count, asio::use_future_t<>)
    {
        return future_for([&](auto callback) { async_send(data, count, std::move(callback)); });
    }
    template <class Data, class Callback>
        requires send_completion_handler<Callback>
    bool async_send(Data&& data, Callback&& callback)
    {
        return submit(std::forward<Data>(data), completion_for(std::forward<Callback>(callback)), sender().selfptr());
    }
    template <class Character, class Callback>
        requires(is_char_v<Character> && send_completion_handler<Callback>)
    bool async_send(Character* data, Callback&& callback)
    {
        return async_send(data, string_length(data), std::forward<Callback>(callback));
    }
    template <class Element, std::integral Count, class Callback>
        requires send_completion_handler<Callback>
    bool async_send(Element* data, Count count, Callback&& callback)
    {
        if (!data || (std::is_signed_v<Count> && count < 0) ||
            static_cast<std::uintmax_t>(count) > std::numeric_limits<std::size_t>::max() / sizeof(Element))
            return reject(callback, asio::error::invalid_argument);
        const auto bytes = static_cast<std::size_t>(count) * sizeof(Element);
        if (!this->_can_send_buffer_size(bytes))
            return reject(callback, asio::error::no_buffer_space);
        if constexpr (is_char_v<Element>)
            return async_send(std::basic_string_view<std::remove_cv_t<Element>>(data, static_cast<std::size_t>(count)),
                              std::forward<Callback>(callback));
        else
            return async_send(asio::const_buffer(data, bytes), std::forward<Callback>(callback));
    }
    template <class Data> std::size_t send(Data&& data)
    {
        return wait_send(sender().async_send(std::forward<Data>(data), asio::use_future));
    }
    template <class Character>
        requires is_char_v<Character>
    std::size_t send(Character* data)
    {
        return send(data, string_length(data));
    }
    template <class Element, std::integral Count> std::size_t send(Element* data, Count count)
    {
        return wait_send(sender().async_send(data, count, asio::use_future));
    }

protected:
    template <class Data> void internal_async_send(std::shared_ptr<Derived> owner, Data&& data)
    {
        internal_async_send(std::move(owner), std::forward<Data>(data), ignore_completion{});
    }
    template <class Data, class Callback>
        requires send_completion_handler<Callback>
    void internal_async_send(std::shared_ptr<Derived> owner, Data&& data, Callback&& callback)
    {
        submit(std::forward<Data>(data), completion_for(std::forward<Callback>(callback)), std::move(owner));
    }
    template <class Data, class Callback>
    void internal_async_send(std::shared_ptr<Derived> owner, Data&& data, Callback&& callback,
                             event_queue_guard<Derived> guard)
    {
        submit(std::forward<Data>(data), std::forward<Callback>(callback), std::move(owner), std::move(guard));
    }

private:
    struct ignore_completion
    {
        void operator()(const error_code&, std::size_t) const noexcept {}
    };
    Derived& sender() noexcept { return static_cast<Derived&>(*this); }
    template <class Character> static std::size_t string_length(const Character* data)
    {
        return data ? std::char_traits<std::remove_cv_t<Character>>::length(data) : 0;
    }
    template <class Callback> static bool reject(Callback& callback, error_code error)
    {
        set_last_error(error);
        _call_completion(callback, error, 0);
        return false;
    }
    template <class Submit> static auto future_for(Submit&& operation)
    {
        std::promise<result_type> promise;
        auto future = promise.get_future();
        operation([promise = std::move(promise)](const error_code& error, std::size_t bytes) mutable
                  { promise.set_value({error, bytes}); });
        return future;
    }
    template <class Callback> static auto completion_for(Callback&& callback)
    {
        return [callback = std::forward<Callback>(callback)](std::shared_ptr<Derived>, const error_code& error,
                                                             std::size_t bytes, event_queue_guard<Derived>) mutable
        { _call_completion(callback, error, bytes); };
    }
    template <class Data> std::size_t payload_size(const Data& data)
    {
        if constexpr (requires { sender()._send_data_size(data); })
            return sender()._send_data_size(data);
        else if constexpr (is_span<std::remove_cvref_t<Data>>::value)
            return data.size_bytes();
        else
            return asio::buffer_size(asio::buffer(data));
    }
    template <class Data, class Callback>
    bool submit(Data&& input, Callback&& callback, std::shared_ptr<Derived> owner,
                event_queue_guard<Derived> guard = {})
    {
        auto& object = sender();
        std::optional<integer_add_sub_guard<std::atomic<std::size_t>>> pending(std::in_place, object.io_->pending());
        const auto generation = object.life_id();
        auto fail = [&](error_code error)
        {
            set_last_error(error);
            pending.reset();
            callback(std::move(owner), error, 0, std::move(guard));
            return false;
        };
        if (object.state_ != state_t::started)
            return fail(asio::error::not_connected);
        const auto input_bytes = payload_size(input);
        if (!this->_reserve_send_buffer(input_bytes))
            return fail(asio::error::no_buffer_space);
        send_buffer_guard reservation(*this, input_bytes);
        auto data = object._data_persistence(std::forward<Data>(input));
        if (!reservation.resize(asio::buffer_size(asio::buffer(data))))
        {
            reservation.release();
            return fail(asio::error::no_buffer_space);
        }
        auto payload = std::make_unique<decltype(data)>(std::move(data));
        object.disp_event(
            [&object, generation, owner = std::move(owner), callback = std::forward<Callback>(callback),
             payload = std::move(payload),
             reservation = std::move(reservation)](event_queue_guard<Derived> current) mutable
            {
                error_code error;
                if (!object.is_started())
                    error = asio::error::not_connected;
                else if (generation != object.life_id())
                    error = asio::error::operation_aborted;
                if (error)
                {
                    set_last_error(error);
                    reservation.release();
                    callback(std::move(owner), error, 0, std::move(current));
                    return;
                }
                clear_last_error();
                // Keep the payload address stable across the event queue and transport completion.
                auto* buffer = payload.get();
                object._do_send(*buffer,
                                [callback = std::move(callback), reservation = std::move(reservation),
                                 payload = std::move(payload), owner = std::move(owner),
                                 current = std::move(current)](const error_code& error, std::size_t bytes) mutable
                                {
                                    reservation.release();
                                    callback(std::move(owner), error, bytes, std::move(current));
                                });
            },
            std::move(guard));
        return true;
    }
    std::size_t wait_send(std::future<result_type> future)
    {
        if (sender().io_->context().get_executor().running_in_this_thread() &&
            future.wait_for(std::chrono::nanoseconds::zero()) != std::future_status::ready)
        {
            set_last_error(asio::error::in_progress);
            return 0;
        }
        auto [error, bytes] = future.get();
        set_last_error(error);
        return bytes;
    }
};
}
