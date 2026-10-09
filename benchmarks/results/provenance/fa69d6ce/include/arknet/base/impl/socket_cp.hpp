// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <memory>
#include <string>
#include <type_traits>
#include <arknet/base/error.hpp>
namespace arknet::detail
{
template<class Derived, class Args> class socket_cp
{
public:
    using socket_type = std::remove_cvref_t<typename Args::socket_t>;
    explicit socket_cp(asio::io_context& context) : socket_(std::make_shared<socket_type>(context)) {}
    explicit socket_cp(asio::any_io_executor executor) : socket_(std::make_shared<socket_type>(executor)) {}
    explicit socket_cp(std::shared_ptr<typename Args::socket_t> socket) : socket_(std::move(socket)) {}
    socket_type& socket() noexcept { return *socket_; }
    const socket_type& socket() const noexcept { return *socket_; }
    socket_type& stream() noexcept { return *socket_; }
    const socket_type& stream() const noexcept { return *socket_; }
    std::string local_address() const noexcept { return get_local_address(); }
    std::string get_local_address() const noexcept { const auto endpoint = socket_->lowest_layer().local_endpoint(get_last_error()); return get_last_error() ? std::string{} : endpoint.address().to_string(); }
    unsigned short local_port() const noexcept { return get_local_port(); }
    unsigned short get_local_port() const noexcept { return socket_->lowest_layer().local_endpoint(get_last_error()).port(); }
    std::string remote_address() const noexcept { return get_remote_address(); }
    std::string get_remote_address() const noexcept { const auto endpoint = remote(); return get_last_error() ? std::string{} : endpoint.address().to_string(); }
    unsigned short remote_port() const noexcept { return get_remote_port(); }
    unsigned short get_remote_port() const noexcept { return remote().port(); }
    Derived& set_sndbuf_size(int value) noexcept { return set_option(asio::socket_base::send_buffer_size(value)); }
    int get_sndbuf_size() const noexcept { return get_option<asio::socket_base::send_buffer_size>().value(); }
    Derived& set_rcvbuf_size(int value) noexcept { return set_option(asio::socket_base::receive_buffer_size(value)); }
    int get_rcvbuf_size() const noexcept { return get_option<asio::socket_base::receive_buffer_size>().value(); }
    Derived& set_keep_alive(bool value) noexcept { return set_option(asio::socket_base::keep_alive(value)); }
    bool is_keep_alive() const noexcept { return get_option<asio::socket_base::keep_alive>().value(); }
    Derived& keep_alive(bool value) noexcept { return set_keep_alive(value); }
    Derived& set_reuse_address(bool value) noexcept { return set_option(asio::socket_base::reuse_address(value)); }
    bool is_reuse_address() const noexcept { return get_option<asio::socket_base::reuse_address>().value(); }
    Derived& reuse_address(bool value) noexcept { return set_reuse_address(value); }
    Derived& set_no_delay(bool value) noexcept { return set_option(asio::ip::tcp::no_delay(value)); }
    bool is_no_delay() const noexcept { return get_option<asio::ip::tcp::no_delay>().value(); }
    Derived& no_delay(bool value) noexcept { return set_no_delay(value); }
    Derived& set_linger(bool enabled, int seconds) noexcept { return set_option(asio::socket_base::linger(enabled, seconds)); }
    asio::socket_base::linger get_linger() const noexcept { return get_option<asio::socket_base::linger>(); }
protected:
    std::shared_ptr<typename Args::socket_t> socket_;
    typename socket_type::endpoint_type remote_endpoint_{};
private:
    auto remote() const noexcept
    {
        auto endpoint = socket_->lowest_layer().remote_endpoint(get_last_error());
        if (get_last_error() && !remote_endpoint_.address().is_unspecified()) { clear_last_error(); return remote_endpoint_; }
        return endpoint;
    }
    template<class Option> Derived& set_option(const Option& option) noexcept { socket_->lowest_layer().set_option(option, get_last_error()); return static_cast<Derived&>(*this); }
    template<class Option> Option get_option() const noexcept { Option option; socket_->lowest_layer().get_option(option, get_last_error()); return option; }
};
}
