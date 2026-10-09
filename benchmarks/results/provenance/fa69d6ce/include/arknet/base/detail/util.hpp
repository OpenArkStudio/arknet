// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <arknet/base/error.hpp>
#include <arknet/base/detail/filesystem.hpp>
#include <arknet/base/detail/shared_mutex.hpp>
#include <arknet/base/detail/type_traits.hpp>
#include <arknet/util/string.hpp>
#ifndef ARKNET_DEFAULT_SSL_METHOD
#define ARKNET_DEFAULT_SSL_METHOD asio::ssl::context::sslv23
#endif
namespace arknet
{
template<class Buffer> requires (std::is_convertible_v<Buffer, asio::const_buffer>)
std::string_view to_string_view(const Buffer& buffer) noexcept
{
    const asio::const_buffer bytes(buffer);
    return bytes.size() ? std::string_view(static_cast<const char*>(bytes.data()), bytes.size()) : std::string_view{};
}
template<class Buffer> requires (std::is_convertible_v<Buffer, asio::const_buffer>)
std::string to_string(const Buffer& buffer) { return std::string(to_string_view(buffer)); }
template<class Endpoint, class Host, class Service> Endpoint to_endpoint(Host&& host, Service&& service)
{
    using Protocol = typename Endpoint::protocol_type;
    asio::io_context context;
    asio::ip::basic_resolver<Protocol> resolver(context);
    error_code error;
    auto results = resolver.resolve(to_string(std::forward<Host>(host)), to_string(std::forward<Service>(service)), asio::ip::resolver_base::address_configured, error);
    set_last_error(error ? error : results.empty() ? error_code(asio::error::host_not_found) : error_code{});
    return get_last_error() ? Endpoint{} : results.begin()->endpoint();
}
enum class net_protocol : std::uint8_t { none, udp, tcp, websocket, tcps, websockets, ws = websocket, wss = websockets };
template<class... Functions> struct variant_overloaded : Functions... { using Functions::operator()...; };
}
namespace arknet::detail
{
using arknet::to_string;
using arknet::to_string_view;
using arknet::to_endpoint;
struct tcp_tag { using tl_tag_type = tcp_tag; };
struct udp_tag { using tl_tag_type = udp_tag; };
struct cast_tag { using tl_tag_type = cast_tag; };
struct ssl_stream_tag {};
struct ws_stream_tag {};
enum class state_t : std::int8_t { stopped, stopping, starting, started };
template<class = void> constexpr std::string_view to_string(state_t state)
{
    constexpr std::array<std::string_view, 4> labels{"stopped", "stopping", "starting", "started"};
    const auto index = static_cast<std::size_t>(state);
    return index < labels.size() ? labels[index] : "none";
}
inline constexpr long tcp_handshake_timeout = 30000, udp_handshake_timeout = 30000, http_handshake_timeout = 30000;
inline constexpr long tcp_connect_timeout = 30000, udp_connect_timeout = 30000, http_connect_timeout = 30000;
inline constexpr long tcp_silence_timeout = 3600000, udp_silence_timeout = 60000, http_silence_timeout = 85000;
inline constexpr long ssl_shutdown_timeout = 30000, ws_shutdown_timeout = 30000;
inline constexpr long ssl_handshake_timeout = 30000, ws_handshake_timeout = 30000;
inline constexpr std::size_t tcp_frame_size = 1536, udp_frame_size = 65536, http_frame_size = 1536;
inline constexpr std::size_t max_buffer_size = 16 * 1024 * 1024;
template<class = void> std::size_t default_concurrency() noexcept { return std::max(2u, std::thread::hardware_concurrency() * 2); }
template<class = void> std::size_t bkdr_hash(const unsigned char* bytes, std::size_t size) noexcept
{
    std::size_t value = 0;
    for (auto byte : std::span(bytes, size)) value = value * 131 + byte;
    return value;
}
template<class Integer> Integer fnv1a_hash(Integer value, const unsigned char* bytes, Integer count) noexcept
{
    constexpr Integer multiplier = sizeof(Integer) == 4 ? Integer(16777619u) : Integer(1099511628211ull);
    for (auto byte : std::span(bytes, std::size_t(count))) value = (value ^ byte) * multiplier;
    return value;
}
template<class Integer> Integer fnv1a_hash(const unsigned char* bytes, Integer count) noexcept
{
    constexpr Integer basis = sizeof(Integer) == 4 ? Integer(2166136261u) : Integer(14695981039346656037ull);
    return fnv1a_hash(basis, bytes, count);
}
template<class Value> class copyable_wrapper
{
public:
    using value_type = Value;
    template<class... Args> explicit copyable_wrapper(Args&&... args) : value_(std::make_shared<Value>(std::forward<Args>(args)...)) {}
    Value& operator()() noexcept { return *value_; }
private:
    std::shared_ptr<Value> value_;
};
template<class Value, class = void> struct is_copyable_wrapper : is_template_instance_of<copyable_wrapper, Value> {};
template<class Value> inline constexpr bool is_copyable_wrapper_v = is_copyable_wrapper<Value>::value;
inline void cancel_timer(asio::steady_timer& timer) noexcept { try { timer.cancel(); } catch (const system_error&) {} }
template<class Rep, class Period> asio::steady_timer::duration to_steady_duration(std::chrono::duration<Rep, Period> input) noexcept
{
    using Duration = asio::steady_timer::duration;
    using Wide = std::chrono::duration<long double, typename Duration::period>;
    const auto ticks = Wide(input).count();
    const auto maximum = Wide(Duration::max()).count();
    const auto minimum = Wide(Duration::min()).count();
    if (ticks >= maximum) return Duration::max();
    if (ticks <= minimum) return Duration::min();
    return Duration(static_cast<typename Duration::rep>(ticks));
}
struct safe_timer
{
    explicit safe_timer(asio::io_context& context) : timer(context) {}
    explicit safe_timer(asio::any_io_executor executor) : timer(executor) {}
    void cancel() { canceled.test_and_set(std::memory_order_release); cancel_timer(timer); }
    asio::steady_timer timer;
    std::atomic_flag canceled = ATOMIC_FLAG_INIT;
};
template<class Duration, class Function> struct repeating_timer_state : std::enable_shared_from_this<repeating_timer_state<Duration, Function>>
{
    std::shared_ptr<safe_timer> timer;
    Duration interval;
    Function callback;
    repeating_timer_state(std::shared_ptr<safe_timer> input, Duration duration, Function function) : timer(std::move(input)), interval(duration), callback(std::move(function)) {}
    void arm()
    {
        timer->timer.expires_after(interval);
        timer->timer.async_wait([self = this->shared_from_this()](const error_code& error)
        {
            if (!self->timer->canceled.test(std::memory_order_acquire) && self->callback(error)) self->arm();
        });
    }
};
template<class Rep, class Period, class Function> auto mktimer(asio::io_context& context, std::chrono::duration<Rep, Period> interval, Function&& callback)
{
    auto timer = std::make_shared<safe_timer>(context);
    auto state = std::make_shared<repeating_timer_state<decltype(interval), std::decay_t<Function>>>(timer, interval, std::forward<Function>(callback));
    state->arm();
    return timer;
}
template<class Integer, bool Integral = true, bool Unsigned = true, bool SkipZero = true> class id_maker
{
public:
    explicit id_maker(Integer first = 1) noexcept : next_(first) {}
    Integer mkid() noexcept
    {
        auto value = next_.fetch_add(1, std::memory_order_relaxed);
        if constexpr (SkipZero) { while (value == 0) value = next_.fetch_add(1, std::memory_order_relaxed); }
        return value;
    }
private:
    std::atomic<Integer> next_;
};
template<class = void> constexpr bool is_little_endian() noexcept { return std::endian::native == std::endian::little; }
template<std::size_t Size> void swap_bytes(std::uint8_t* bytes) noexcept { std::reverse(bytes, bytes + Size); }
template<class Value> void swap_bytes(Value& value) noexcept { swap_bytes<sizeof(Value)>(reinterpret_cast<std::uint8_t*>(std::addressof(value))); }
template<class Value> Value host_to_network(Value value) noexcept { if constexpr (std::endian::native == std::endian::little) swap_bytes(value); return value; }
template<class Value> Value network_to_host(Value value) noexcept { return host_to_network(value); }
template<class Value, class Pointer> void write(Pointer& destination, Value value) noexcept
{
    const auto encoded = host_to_network(value);
    std::memcpy(destination, std::addressof(encoded), sizeof(Value));
    destination += sizeof(Value);
}
template<class Value, class Pointer> Value read(Pointer& source) noexcept
{
    Value encoded;
    std::memcpy(std::addressof(encoded), source, sizeof(Value));
    source += sizeof(Value);
    return network_to_host(encoded);
}
template<class String> std::size_t sso_buffer_size() noexcept { return String{}.capacity(); }
template<class String> bool is_used_sso(const String& value) noexcept { return value.capacity() == sso_buffer_size<String>(); }
template<class String> void disable_sso(String& value) { value.reserve(sso_buffer_size<String>() + 1); }
template<class Integer> struct integer_add_sub_guard
{
    explicit integer_add_sub_guard(Integer& value) noexcept : value_(value) { ++value_; }
    ~integer_add_sub_guard() { --value_; }
    Integer& value_;
};
template<class = void> bool is_subpath_of(const std::filesystem::path& base, const std::filesystem::path& path) noexcept
{
    auto position = path.begin();
    for (const auto& component : base) { if (position == path.end() || *position++ != component) return false; }
    return position != path.end();
}
template<class = void> std::filesystem::path make_filepath(const std::filesystem::path& base, const std::filesystem::path& path) noexcept
{
    std::error_code error;
    const auto root = std::filesystem::canonical(base, error);
    if (error) return {};
    const auto result = std::filesystem::canonical(root / path.relative_path(), error);
    return !error && is_subpath_of(root, result) ? result : std::filesystem::path{};
}
template<class Value> struct current_object_result_t : std::type_identity<Value&> {};
template<class Value> struct current_object_result_t<std::shared_ptr<Value>> : std::type_identity<std::weak_ptr<Value>&> {};
template<class Value> typename current_object_result_t<Value>::type get_current_object() noexcept
{
    if constexpr (is_template_instance_of_v<std::shared_ptr, Value>) { thread_local std::weak_ptr<typename Value::element_type> current; return current; }
    else { thread_local Value current{}; return current; }
}
struct wait_timer_op
{
    asio::steady_timer& timer_;
    bool waiting_ = false;
    explicit wait_timer_op(asio::steady_timer& timer) : timer_(timer) {}
    template<class Operation> void operator()(Operation& operation, error_code = {})
    {
        if (std::exchange(waiting_, true)) operation.complete(get_last_error());
        else timer_.async_wait(std::move(operation));
    }
};
struct data_filter_before_helper
{
    template<class T, class = void> struct has_member_data_filter_before_recv : std::bool_constant<requires(T& object, std::string_view bytes) { object.data_filter_before_recv(bytes); }> {};
    template<class T, class = void> struct has_member_data_filter_before_send : std::bool_constant<requires(T& object, std::string_view bytes) { object.data_filter_before_send(bytes); }> {};
    template<class Object> static std::string_view call_data_filter_before_recv(Object& object, std::string_view bytes) noexcept
    {
        if constexpr (requires { object.data_filter_before_recv(bytes); }) return object.data_filter_before_recv(bytes);
        else return bytes;
    }
    template<class Object, class Data> static auto call_data_filter_before_send(Object& object, Data&& data)
    {
        if constexpr (requires { object.data_filter_before_send(std::forward<Data>(data)); }) return object.data_filter_before_send(std::forward<Data>(data));
        else return std::forward<Data>(data);
    }
};
template<class Object> std::string_view call_data_filter_before_recv(Object& object, std::string_view data) noexcept { return data_filter_before_helper::call_data_filter_before_recv(object, data); }
template<class Object, class Data> auto call_data_filter_before_send(Object& object, Data&& data) { return data_filter_before_helper::call_data_filter_before_send(object, std::forward<Data>(data)); }
template<class T, class = void> struct has_member_insert : std::bool_constant<requires(T& container, std::string_view value) { container.insert(container.begin(), value.begin(), value.end()); }> {};
}
namespace arknet
{
template<class T> T get_current_caller() noexcept
{
    if constexpr (detail::is_template_instance_of_v<std::shared_ptr, T>) return detail::get_current_object<T>().lock();
    else if constexpr (std::is_reference_v<T>) return *detail::get_current_object<std::remove_reference_t<T>*>();
    else return detail::get_current_object<T>();
}
}
