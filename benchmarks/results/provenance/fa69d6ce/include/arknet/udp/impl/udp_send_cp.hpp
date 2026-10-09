// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <future>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <arknet/base/define.hpp>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/impl/data_persistence_cp.hpp>
#include <arknet/base/impl/send_queue_cp.hpp>

namespace arknet::detail
{
ARKNET_CLASS_FORWARD_DECLARE_BASE;

template<class Derived, class Args>
class udp_send_cp : public data_persistence_cp<Derived, Args>, public send_queue_cp<Derived, Args>
{
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    using endpoint_type = asio::ip::udp::endpoint;
    using address_type = std::pair<std::string, std::string>;
    using reservation_type = typename send_queue_cp<Derived, Args>::send_buffer_guard;

public:
    template<class Data>
    bool async_send(endpoint_type endpoint, Data&& data)
    {
        return submit(std::move(endpoint), std::forward<Data>(data), [](const error_code&, std::size_t) {});
    }
    template<class Data, class Completion> requires send_completion_handler<Completion>
    bool async_send(endpoint_type endpoint, Data&& data, Completion&& completion)
    {
        return submit(std::move(endpoint), std::forward<Data>(data), std::forward<Completion>(completion));
    }
    template<class Data>
    auto async_send(endpoint_type endpoint, Data&& data, asio::use_future_t<>)
    {
        return future_for([&](auto completion) { async_send(std::move(endpoint), std::forward<Data>(data), std::move(completion)); });
    }
    template<class Element, class Count> requires std::is_integral_v<remove_cvref_t<Count>>
    bool async_send(endpoint_type endpoint, Element* data, Count count)
    {
        return submit_range(std::move(endpoint), data, count, [](const error_code&, std::size_t) {});
    }
    template<class Element, class Count, class Completion>
        requires (std::is_integral_v<remove_cvref_t<Count>> && send_completion_handler<Completion>)
    bool async_send(endpoint_type endpoint, Element* data, Count count, Completion&& completion)
    {
        return submit_range(std::move(endpoint), data, count, std::forward<Completion>(completion));
    }
    template<class Element, class Count> requires std::is_integral_v<remove_cvref_t<Count>>
    auto async_send(endpoint_type endpoint, Element* data, Count count, asio::use_future_t<>)
    {
        return future_for([&](auto completion) { async_send(std::move(endpoint), data, count, std::move(completion)); });
    }

    template<class Host, class Service, class Data> requires (!std::is_same_v<remove_cvref_t<Host>, endpoint_type>)
    bool async_send(Host&& host, Service&& service, Data&& data)
    {
        return async_send(std::forward<Host>(host), std::forward<Service>(service),
            std::forward<Data>(data), [](const error_code&, std::size_t) {});
    }
    template<class Host, class Service, class Data, class Completion>
        requires (!std::is_same_v<remove_cvref_t<Host>, endpoint_type> && send_completion_handler<Completion>)
    bool async_send(Host&& host, Service&& service, Data&& data, Completion&& completion)
    {
        return submit(address_type{detail::to_string(std::forward<Host>(host)), detail::to_string(std::forward<Service>(service))},
            std::forward<Data>(data), std::forward<Completion>(completion));
    }
    template<class Host, class Service, class Data> requires (!std::is_same_v<remove_cvref_t<Host>, endpoint_type>)
    auto async_send(Host&& host, Service&& service, Data&& data, asio::use_future_t<>)
    {
        return future_for([&](auto completion) { async_send(std::forward<Host>(host), std::forward<Service>(service),
            std::forward<Data>(data), std::move(completion)); });
    }
    template<class Host, class Service, class Element, class Count>
        requires (!std::is_same_v<remove_cvref_t<Host>, endpoint_type> && std::is_integral_v<remove_cvref_t<Count>>)
    bool async_send(Host&& host, Service&& service, Element* data, Count count)
    {
        return async_send(std::forward<Host>(host), std::forward<Service>(service), data, count,
            [](const error_code&, std::size_t) {});
    }
    template<class Host, class Service, class Element, class Count, class Completion>
        requires (!std::is_same_v<remove_cvref_t<Host>, endpoint_type> && std::is_integral_v<remove_cvref_t<Count>> && send_completion_handler<Completion>)
    bool async_send(Host&& host, Service&& service, Element* data, Count count, Completion&& completion)
    {
        return submit_range(address_type{detail::to_string(std::forward<Host>(host)), detail::to_string(std::forward<Service>(service))},
            data, count, std::forward<Completion>(completion));
    }
    template<class Host, class Service, class Element, class Count>
        requires (!std::is_same_v<remove_cvref_t<Host>, endpoint_type> && std::is_integral_v<remove_cvref_t<Count>>)
    auto async_send(Host&& host, Service&& service, Element* data, Count count, asio::use_future_t<>)
    {
        return future_for([&](auto completion) { async_send(std::forward<Host>(host), std::forward<Service>(service),
            data, count, std::move(completion)); });
    }

    template<class... Input>
    std::size_t send(Input&&... input)
    {
        auto completion = async_send(std::forward<Input>(input)..., asio::use_future);
        auto& owner = static_cast<Derived&>(*this);
        if (owner.io_->context().get_executor().running_in_this_thread() &&
            completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            set_last_error(asio::error::in_progress);
            return 0;
        }
        auto [error, count] = completion.get();
        set_last_error(error);
        return count;
    }

private:
    template<class Start>
    static auto future_for(Start&& start)
    {
        std::promise<std::pair<error_code, std::size_t>> promise;
        auto future = promise.get_future();
        start([promise = std::move(promise)](const error_code& error, std::size_t count) mutable { promise.set_value({error, count}); });
        return future;
    }
    template<class Completion>
    bool reject(Completion& completion, const error_code& error)
    {
        set_last_error(error);
        this->_call_completion(completion, error, 0);
        return false;
    }
    template<class Destination, class Element, class Count, class Completion>
    bool submit_range(Destination destination, Element* data, Count count, Completion&& completion)
    {
        if (!data || (std::is_signed_v<Count> && count < 0) ||
            static_cast<std::uintmax_t>(count) > std::numeric_limits<std::size_t>::max() / sizeof(Element))
            return reject(completion, asio::error::invalid_argument);
        const auto bytes = static_cast<std::size_t>(count) * sizeof(Element);
        if (!this->_can_send_buffer_size(bytes))
            return reject(completion, asio::error::no_buffer_space);
        return submit(std::move(destination), asio::const_buffer(data, bytes), std::forward<Completion>(completion));
    }

    template<class Destination, class Data, class Completion>
    bool submit(Destination destination, Data&& input, Completion&& completion)
    {
        if constexpr (is_char_pointer_v<remove_cvref_t<Data>>)
            if (!input)
                return reject(completion, asio::error::invalid_argument);
        auto& owner = static_cast<Derived&>(*this);
        std::optional<integer_add_sub_guard<std::atomic<std::size_t>>> pending(std::in_place, owner.io_->pending());
        const auto generation = owner.life_id();
        if (owner.state_ != state_t::started)
        {
            pending.reset();
            return reject(completion, asio::error::not_connected);
        }
        const auto input_size = [&]
        {
            if constexpr (is_span<remove_cvref_t<Data>>::value)
                return input.size_bytes();
            else if constexpr (is_char_array_v<remove_cvref_t<Data>>)
            {
                std::size_t count = 0;
                while (count < std::extent_v<remove_cvref_t<Data>> && input[count] != std::remove_cv_t<std::remove_all_extents_t<remove_cvref_t<Data>>>{})
                    ++count;
                return count * sizeof(*input);
            }
            else if constexpr (is_char_pointer_v<remove_cvref_t<Data>>)
                return std::char_traits<remove_cvref_t<std::remove_pointer_t<std::decay_t<Data>>>>::length(input) * sizeof(*input);
            else
                return asio::buffer_size(asio::buffer(input));
        }();
        if (!this->_can_send_buffer_size(input_size))
        {
            pending.reset();
            return reject(completion, asio::error::no_buffer_space);
        }
        auto payload = owner._data_persistence(std::forward<Data>(input));
        const auto bytes = asio::buffer_size(asio::buffer(payload));
        if (!this->_reserve_send_buffer(bytes))
        {
            pending.reset();
            return reject(completion, asio::error::no_buffer_space);
        }
        struct request
        {
            decltype(payload) data;
            remove_cvref_t<Completion> callback;
            reservation_type reservation;
            std::shared_ptr<Derived> lifetime;
        };
        reservation_type reservation(*this, bytes);
        auto operation = std::make_shared<request>(request{std::move(payload), std::forward<Completion>(completion),
            std::move(reservation), owner.selfptr()});
        owner.push_event([this, &owner, destination = std::move(destination), generation, operation]
            (event_queue_guard<Derived> guard) mutable
            {
                auto finish = [this, operation](const error_code& error, std::size_t count) mutable
                {
                    operation->reservation.release();
                    set_last_error(error);
                    this->_call_completion(operation->callback, error, count);
                };
                if (!owner.is_started() || generation != owner.life_id())
                {
                    finish(owner.is_started() ? error_code(asio::error::operation_aborted) : error_code(asio::error::not_connected), 0);
                    return;
                }
                if constexpr (std::is_same_v<Destination, endpoint_type>)
                {
                    owner._do_send(destination, operation->data,
                        [finish = std::move(finish), guard = std::move(guard)](const error_code& error, std::size_t count) mutable
                        { finish(error, count); });
                }
                else
                {
                    auto resolver = std::make_shared<asio::ip::udp::resolver>(owner.io_->executor());
                    resolver->async_resolve(destination.first, destination.second,
                        [&owner, generation, resolver, operation, finish = std::move(finish), guard = std::move(guard)]
                        (const error_code& error, const asio::ip::udp::resolver::results_type& endpoints) mutable
                        {
                            if (error) { finish(error, 0); return; }
                            if (!owner.is_started() || generation != owner.life_id())
                            { finish(asio::error::operation_aborted, 0); return; }
                            error_code endpoint_error;
                            auto local = owner.socket().local_endpoint(endpoint_error);
                            if (endpoint_error) { finish(endpoint_error, 0); return; }
                            for (const auto& result : endpoints)
                            {
                                if (result.endpoint().protocol() != local.protocol())
                                    continue;
                                owner._do_send(result.endpoint(), operation->data,
                                    [finish = std::move(finish), guard = std::move(guard)](const error_code& error, std::size_t count) mutable
                                    { finish(error, count); });
                                return;
                            }
                            finish(asio::error::host_not_found, 0);
                        });
                }
            });
        return true;
    }
};
}
