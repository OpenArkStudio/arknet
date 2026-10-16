// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>

#else
#include <signal.h>
#include <unistd.h>

#endif

#include <arknet/base/io_pool.hpp>
#include <arknet/tcp/tcp_client.hpp>
#include <arknet/tcp/tcp_server.hpp>
#include <arknet/version.hpp>
#include "connections_protocol.hpp"
#include "connections_resources.hpp"

namespace {

using clock_type = std::chrono::steady_clock;
using namespace std::chrono_literals;
using connection_load::frame_header;
using connection_load::frame_kind;
using connection_load::header_bytes;
using connection_load::identity;

std::atomic<std::sig_atomic_t> interrupted{};
static_assert(decltype(interrupted)::is_always_lock_free);

void request_interrupt(int signal) {
    interrupted.store(signal, std::memory_order_relaxed);
}

struct options
{
    std::string role, scenario = "idle", host;
    std::uint64_t run{}, client{};
    std::size_t port{}, connections = 1, expected{}, maximum = 4096;
    std::size_t rate = 100, parallel = 32, threads = 1, payload = 1024, window = 1;
    std::size_t connect_timeout_ms = 5000, request_timeout_ms = 5000, sample_ms = 1000;
    std::size_t maximum_buffer = 4096, inflight_limit = 64 * 1024 * 1024, buffer_budget = 512 * 1024 * 1024;
    double hold_seconds = 1, deadline_seconds = 30, cleanup_seconds = 5;
};

std::uint64_t integer(std::string_view text) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        throw std::invalid_argument("expected an unsigned decimal integer");
    }
    return value;
}

double duration(const std::string& text) {
    char* end{};
    const auto value = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(value))
    {
        throw std::invalid_argument("expected a finite duration");
    }
    return value;
}

void help() {
    std::cout << "arknet_connection_benchmark --role server|client --scenario idle|traffic\n"
                 "  server: --listen NUMERIC_IP --port PORT --run-id ID [--expected-connections COUNT]\n"
                 "  client: --target NUMERIC_IP --port PORT --run-id ID [--client-id ID] --connections COUNT\n"
                 "  --connect-rate COUNT_PER_SECOND --connect-parallel COUNT --connect-timeout-ms MS\n"
                 "  --hold-seconds SECONDS --deadline-seconds SECONDS --cleanup-seconds SECONDS\n"
                 "  --request-timeout-ms MS --io-threads COUNT --payload BYTES --window COUNT\n"
                 "  --max-connections COUNT --max-buffer-bytes BYTES --max-inflight-bytes BYTES\n"
                 "  --buffer-budget-bytes BYTES --sample-ms MS\n"
                 "TCP only; traffic is closed-loop. IO pool uses N contexts with one worker each.\n"
                 "Server port 0 selects an ephemeral port, reported in its ready JSON line.\n"
                 "Server expected=0 observes until deadline; it does not certify a target count.\n"
                 "Buffer budget covers declared receive buffers, send bytes and timestamp rings;\n"
                 "it is not an RSS or operating-system socket-memory limit. See CONNECTIONS.md.\n";
}

options arguments(int argc, char** argv) {
    options result;
#ifdef _WIN32
    result.client = GetCurrentProcessId();
#else
    result.client = static_cast<std::uint64_t>(getpid());
#endif
    bool port_set = false;
    std::string address_option;
    for (int index = 1; index < argc; ++index)
    {
        const std::string key = argv[index];
        if (key == "--help")
        {
            help();
            std::exit(0);
        }
        if (++index == argc)
        {
            throw std::invalid_argument("missing value for " + key);
        }
        const std::string value = argv[index];
        if (key == "--role")
        {
            result.role = value;
        }
        else if (key == "--scenario")
        {
            result.scenario = value;
        }
        else if (key == "--listen" || key == "--target")
        {
            result.host = value;
            address_option = key;
        }
        else if (key == "--port")
        {
            result.port = integer(value);
            port_set = true;
        }
        else if (key == "--run-id")
        {
            result.run = integer(value);
        }
        else if (key == "--client-id")
        {
            result.client = integer(value);
        }
        else if (key == "--connections")
        {
            result.connections = integer(value);
        }
        else if (key == "--expected-connections")
        {
            result.expected = integer(value);
        }
        else if (key == "--max-connections")
        {
            result.maximum = integer(value);
        }
        else if (key == "--connect-rate")
        {
            result.rate = integer(value);
        }
        else if (key == "--connect-parallel")
        {
            result.parallel = integer(value);
        }
        else if (key == "--io-threads")
        {
            result.threads = integer(value);
        }
        else if (key == "--payload")
        {
            result.payload = integer(value);
        }
        else if (key == "--window")
        {
            result.window = integer(value);
        }
        else if (key == "--connect-timeout-ms")
        {
            result.connect_timeout_ms = integer(value);
        }
        else if (key == "--request-timeout-ms")
        {
            result.request_timeout_ms = integer(value);
        }
        else if (key == "--max-buffer-bytes")
        {
            result.maximum_buffer = integer(value);
        }
        else if (key == "--max-inflight-bytes")
        {
            result.inflight_limit = integer(value);
        }
        else if (key == "--buffer-budget-bytes")
        {
            result.buffer_budget = integer(value);
        }
        else if (key == "--sample-ms")
        {
            result.sample_ms = integer(value);
        }
        else if (key == "--hold-seconds")
        {
            result.hold_seconds = duration(value);
        }
        else if (key == "--deadline-seconds")
        {
            result.deadline_seconds = duration(value);
        }
        else if (key == "--cleanup-seconds")
        {
            result.cleanup_seconds = duration(value);
        }
        else
        {
            throw std::invalid_argument("unknown argument: " + key);
        }
    }
    if ((result.role != "server" && result.role != "client") ||
        (result.scenario != "idle" && result.scenario != "traffic") || !result.run || !result.client || !port_set ||
        result.port > 65535 || (result.role == "client" && !result.port) || result.host.empty() ||
        address_option != (result.role == "server" ? "--listen" : "--target"))
    {
        throw std::invalid_argument("specify role, matching listen/target address, port and nonzero run-id");
    }
    arknet::error_code error;
    const auto address = asio::ip::make_address(result.host, error);
    if (error || address.is_multicast() || (result.role == "client" && address.is_unspecified()))
    {
        throw std::invalid_argument("listen/target must be a valid unicast numeric IPv4 or IPv6 address");
    }
    if (result.maximum < 1 || result.maximum > 200000 || result.connections < 1 ||
        result.connections > result.maximum || result.expected > result.maximum || result.rate < 1 ||
        result.rate > 1000000 || result.parallel < 1 || result.parallel > 4096 || result.threads < 1 ||
        result.threads > 64 || result.payload < 1 || result.payload > 1024 * 1024 || result.window < 1 ||
        result.window > 1024 || result.maximum_buffer < header_bytes + result.payload + 9 ||
        result.maximum_buffer > 16 * 1024 * 1024 || result.connect_timeout_ms < 1 ||
        result.connect_timeout_ms > 3600000 || result.request_timeout_ms < 1 || result.request_timeout_ms > 3600000 ||
        result.hold_seconds <= 0 || result.deadline_seconds <= 0 ||
        (result.role == "client" && result.deadline_seconds <= result.hold_seconds) ||
        result.deadline_seconds > 86400 || result.cleanup_seconds < 1 || result.cleanup_seconds > 60 ||
        result.sample_ms < 100 || result.sample_ms > 60000 || result.inflight_limit < header_bytes + result.payload ||
        result.inflight_limit > 1024ULL * 1024 * 1024 || result.buffer_budget < 4096 ||
        result.buffer_budget > 16ULL * 1024 * 1024 * 1024)
    {
        throw std::invalid_argument("argument out of range; see CONNECTIONS.md for limits");
    }
    const auto count = result.role == "client" ? result.connections : result.maximum;
    const auto timestamps = result.scenario == "traffic" && result.role == "client"
                                ? result.connections * result.window * sizeof(clock_type::time_point)
                                : 0;
    const auto reserved = std::uint64_t(count) * result.maximum_buffer + timestamps + result.inflight_limit;
    if (reserved > result.buffer_budget)
    {
        throw std::invalid_argument("declared receive/send/timestamp buffer reservation exceeds buffer budget");
    }
    if (result.role == "client" && result.scenario == "traffic" &&
        std::uint64_t(result.connections) * result.window * (header_bytes + result.payload) > result.inflight_limit)
    {
        throw std::invalid_argument("connection windows exceed max-inflight-bytes");
    }
    return result;
}

std::string quoted(std::string_view text) {
    std::string result = "\"";
    for (const unsigned char character : text)
    {
        if (character == '"' || character == '\\')
        {
            result += '\\';
        }
        if (character < 32)
        {
            result += '?';
        }
        else
        {
            result += static_cast<char>(character);
        }
    }
    return result + '"';
}

std::int64_t tick() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now().time_since_epoch()).count();
}

void raise_peak(std::atomic<std::uint64_t>& peak, std::uint64_t value) {
    auto current = peak.load();
    while (current < value && !peak.compare_exchange_weak(current, value))
    {
    }
}

struct counters
{
    std::atomic<bool> failed{}, closing{};
    std::atomic<std::uint64_t> attempted{}, connecting{}, established{}, accepted{}, admitted{}, active{}, peak{};
    std::atomic<std::uint64_t> ready{}, started{}, finished{}, disconnected{}, connect_failures{}, timeouts{};
    std::atomic<std::uint64_t> unexpected_disconnects{}, content_errors{}, sequence_errors{}, send_errors{}, rejected{};
    std::atomic<std::uint64_t> sent{}, received{}, pending_sends{}, pending_bytes{}, peak_pending_bytes{},
        outstanding{}, shutdown_cancelled_sends{};
    std::atomic<std::int64_t> work_begin{}, work_end{};
    connection_load::latency_distribution latency;
    std::mutex error_mutex;
    std::string first_error;

    void fail(std::string_view reason) noexcept {
        failed = true;
        try
        {
            std::lock_guard lock(error_mutex);
            if (first_error.empty())
            {
                first_error = reason;
            }
        }
        catch (...)
        {}
    }

    template<class Function>
    void guard(Function&& function) noexcept {
        try
        { function(); }
        catch (const std::exception& error)
        { fail(error.what()); }
        catch (...)
        { fail("unknown callback exception"); }
    }

    void connected() {
        raise_peak(peak, ++active);
    }
};

class cleanup_watchdog
{
public:
    explicit cleanup_watchdog(clock_type::time_point deadline)
        : worker_([this, deadline] {
            std::unique_lock lock(mutex_);
            if (!changed_.wait_until(lock, deadline, [this] { return complete_; }))
            {
            // Neither reporting nor sanitizer exit interceptors may hold
            // up the last-resort failure path on a full output pipe.
#ifdef _WIN32
                TerminateProcess(GetCurrentProcess(), 124);
#else
                if (kill(getpid(), SIGKILL) == 0)
                {
                    // Signal delivery need not precede kill() returning. Do
                    // not race it with the C-runtime fallback exit below.
                    for (;;)
                    {
                        pause();
                    }
                }
#endif
                std::_Exit(124);
            }
        }) {
    }

    ~cleanup_watchdog() {
        {
            std::lock_guard lock(mutex_);
            complete_ = true;
        }
        changed_.notify_one();
        worker_.join();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool complete_{};
    std::thread worker_;
};

class observation
{
public:
    observation(const options& settings, counters& values, clock_type::time_point began)
        : settings_(settings)
        , values_(values)
        , started_(clock_type::now())
        , sampled_(started_)
        , resources_(began) {}

    void sample() {
        const auto now = clock_type::now();
        if (now - sampled_ < std::chrono::milliseconds(settings_.sample_ms))
        {
            return;
        }
        const auto resources = resources_.sample();
        std::cout << "{\"event\":\"sample\",\"role\":" << quoted(settings_.role)
                  << ",\"elapsed_s\":" << std::chrono::duration<double>(now - started_).count()
                  << ",\"active\":" << values_.active << ",\"ready\":" << values_.ready
                  << ",\"pending_send_bytes\":" << values_.pending_bytes;
        connection_load::write_resource_fields(std::cout, resources, false);
        std::cout << "}\n" << std::flush;
        sampled_ = now;
    }

private:
    const options& settings_;
    counters& values_;
    clock_type::time_point started_, sampled_;
    connection_load::resource_observer resources_;
};

template<class Endpoint, class Completion>
void send_frame(
    Endpoint& endpoint, std::string bytes, counters& values, const options& settings, Completion completion) {
    const auto size = bytes.size();
    const auto reserved = values.pending_bytes.fetch_add(size) + size;
    if (reserved > settings.inflight_limit)
    {
        values.pending_bytes -= size;
        ++values.rejected;
        values.fail("global pending-send byte limit exceeded");
        return;
    }
    raise_peak(values.peak_pending_bytes, reserved);
    ++values.pending_sends;
    try
    {
        endpoint.async_send(
            std::move(bytes), [&values, size, completion = std::move(completion)](
                                  const arknet::error_code& error, std::size_t transferred) mutable noexcept {
                --values.pending_sends;
                values.pending_bytes -= size;
                values.guard([&] {
                    if (error || transferred != size)
                    {
                        if (values.closing)
                        {
                            ++values.shutdown_cancelled_sends;
                        }
                        else
                        {
                            ++values.send_errors;
                        }
                        if (!values.closing)
                        {
                            values.fail(error ? error.message() : "short send completion");
                        }
                    }
                    else
                    {
                        completion();
                    }
                });
            });
    }
    catch (...)
    {
        --values.pending_sends;
        values.pending_bytes -= size;
        throw;
    }
}

template<class Endpoint>
void send_frame(Endpoint& endpoint, std::string bytes, counters& values, const options& settings) {
    send_frame(endpoint, std::move(bytes), values, settings, [] {});
}

struct client_connection
{
    client_connection(const options& settings, asio::io_context& context, std::size_t index)
        : endpoint((std::min)(std::size_t{4096}, settings.maximum_buffer), settings.maximum_buffer, context)
        , owner{settings.run, settings.client, index}
        , times(settings.scenario == "traffic" ? settings.window : 0) {}

    arknet::tcp_client endpoint;
    identity owner;
    std::vector<clock_type::time_point> times;
    std::atomic<std::int64_t> pending_since{};
    bool connected{}, hello{}, started{}, draining{}, finish_sent{}, finished{};
    std::uint64_t next_send = 1, next_receive = 1;
};

class client_run
{
public:
    client_run(const options& settings, counters& values, arknet::io_pool& runtime)
        : settings_(settings)
        , values_(values)
        , runtime_(runtime) {
        connections_.reserve(settings.connections);
    }

    ~client_run() {
        stop();
    }

    void launch() {
        auto connection = std::make_unique<client_connection>(
            settings_, runtime_.get_context(connections_.size()), connections_.size() + 1);
        auto* state = connection.get();
        state->endpoint.set_auto_reconnect(false);
        state->endpoint.set_connect_timeout(std::chrono::milliseconds(settings_.connect_timeout_ms));
        state->endpoint.set_disconnect_timeout(std::chrono::duration<double>(settings_.cleanup_seconds));
        state->endpoint.bind_connect([this, state]() noexcept {
            const auto error = arknet::get_last_error();
            values_.guard([&] {
                --values_.connecting;
                if (error)
                {
                    ++values_.connect_failures;
                    if (error == asio::error::timed_out)
                    {
                        ++values_.timeouts;
                    }
                    if (!values_.closing)
                    {
                        values_.fail("connect: " + error.message());
                    }
                    return;
                }
                state->connected = true;
                ++values_.established;
                values_.connected();
                state->endpoint.set_no_delay(true);
                if (arknet::get_last_error())
                {
                    throw std::runtime_error("setting TCP_NODELAY failed");
                }
                state->pending_since = tick();
                send_frame(state->endpoint,
                    connection_load::make_frame(
                        frame_kind::hello, state->owner, settings_.scenario == "traffic" ? 1 : 0),
                    values_, settings_);
            });
        });
        state->endpoint.bind_recv(
            [this, state](std::string_view bytes) noexcept { values_.guard([&] { receive(*state, bytes); }); });
        state->endpoint.bind_disconnect([this, state]() noexcept {
            if (state->connected)
            {
                state->connected = false;
                --values_.active;
                ++values_.disconnected;
                if (!values_.closing && !state->finished)
                {
                    ++values_.unexpected_disconnects;
                    values_.fail("unexpected server disconnect");
                }
            }
        });
        connections_.push_back(std::move(connection));
        ++values_.attempted;
        ++values_.connecting;
        if (!state->endpoint.async_start(settings_.host, settings_.port, arknet::use_dgram))
        {
            --values_.connecting;
            ++values_.connect_failures;
            values_.fail("connect submission rejected");
        }
    }

    void start() {
        for (const auto& connection : connections_)
        {
            auto* state = connection.get();
            state->endpoint.post([this, state] {
                values_.guard([&] {
                    state->pending_since = tick();
                    send_frame(state->endpoint, connection_load::make_frame(frame_kind::start, state->owner), values_,
                        settings_);
                });
            });
        }
    }

    void traffic() {
        values_.work_begin = tick();
        if (settings_.scenario == "idle")
        {
            return;
        }
        for (const auto& connection : connections_)
        {
            auto* state = connection.get();
            state->endpoint.post([this, state] { values_.guard([&] { fill(*state); }); });
        }
    }

    void finish() {
        for (const auto& connection : connections_)
        {
            auto* state = connection.get();
            state->endpoint.post([this, state] {
                values_.guard([&] {
                    state->draining = true;
                    finish_if_drained(*state);
                });
            });
        }
    }

    void check_timeouts() {
        const auto now = tick();
        if (now < next_timeout_scan_)
        {
            return;
        }
        const auto timeout = std::int64_t(settings_.request_timeout_ms) * 1000000;
        next_timeout_scan_ = now + (std::max)(std::int64_t{1000000}, (std::min)(timeout / 4, std::int64_t{50000000}));
        for (const auto& connection : connections_)
        {
            const auto since = connection->pending_since.load();
            if (since && now - since >= timeout)
            {
                ++values_.timeouts;
                values_.fail("protocol response deadline exceeded");
                return;
            }
        }
    }

    void stop() {
        if (stopped_)
        {
            return;
        }
        values_.closing = true;
        for (const auto& connection : connections_)
        {
            connection->endpoint.stop();
        }
        for (const auto& connection : connections_)
        {
            if (!connection->endpoint.is_stopped())
            {
                values_.fail("client did not reach stopped state");
            }
        }
        stopped_ = true;
    }

private:
    void receive(client_connection& state, std::string_view bytes) {
        frame_header header;
        if (!connection_load::decode(bytes, header) || header.owner != state.owner)
        {
            ++values_.content_errors;
            values_.fail("invalid response identity or body");
            return;
        }
        if (header.kind == frame_kind::hello_ack && !state.hello && !state.started && !header.sequence)
        {
            state.hello = true;
            state.pending_since = 0;
            ++values_.ready;
        }
        else if (header.kind == frame_kind::start_ack && state.hello && !state.started && !header.sequence)
        {
            state.started = true;
            state.pending_since = 0;
            ++values_.started;
        }
        else if (header.kind == frame_kind::data && state.started && !state.finished &&
                 header.body_bytes == settings_.payload && header.sequence == state.next_receive &&
                 header.sequence < state.next_send)
        {
            const auto sent = state.times[(state.next_receive - 1) % settings_.window];
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now() - sent).count();
            values_.latency.observe(static_cast<std::uint64_t>((std::max)(std::int64_t{0}, elapsed)));
            ++state.next_receive;
            ++values_.received;
            --values_.outstanding;
            state.pending_since = state.next_receive < state.next_send
                                      ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                                            state.times[(state.next_receive - 1) % settings_.window].time_since_epoch())
                                            .count()
                                      : 0;
            if (!state.draining && !values_.failed && !values_.closing)
            {
                fill(state);
            }
            else
            {
                finish_if_drained(state);
            }
        }
        else if (header.kind == frame_kind::finish_ack && state.finish_sent && !state.finished &&
                 header.sequence == state.next_send - 1)
        {
            state.finished = true;
            state.pending_since = 0;
            values_.work_end = tick();
            ++values_.finished;
        }
        else
        {
            ++values_.sequence_errors;
            values_.fail("response kind, sequence or payload size mismatch");
        }
    }

    void fill(client_connection& state) {
        while (!state.draining && !values_.failed && !values_.closing &&
               state.next_send - state.next_receive < settings_.window)
        {
            if (state.next_send == (std::numeric_limits<std::uint64_t>::max)())
            {
                values_.fail("sequence space exhausted");
                return;
            }
            const auto sequence = state.next_send++;
            state.times[(sequence - 1) % settings_.window] = clock_type::now();
            if (sequence == state.next_receive)
            {
                state.pending_since = tick();
            }
            ++values_.sent;
            ++values_.outstanding;
            send_frame(state.endpoint,
                connection_load::make_frame(frame_kind::data, state.owner, sequence, settings_.payload), values_,
                settings_);
        }
    }

    void finish_if_drained(client_connection& state) {
        if (state.draining && !state.finish_sent && state.next_receive == state.next_send && !values_.failed)
        {
            state.finish_sent = true;
            state.pending_since = tick();
            send_frame(state.endpoint,
                connection_load::make_frame(frame_kind::finish, state.owner, state.next_send - 1), values_, settings_);
        }
    }

    const options& settings_;
    counters& values_;
    arknet::io_pool& runtime_;
    std::vector<std::unique_ptr<client_connection>> connections_;
    std::int64_t next_timeout_scan_{};
    bool stopped_{};
};

struct server_connection
{
    identity owner;
    bool hello{}, started{}, finished{};
    std::uint64_t next_sequence = 1;
};

class server_run
{
public:
    server_run(const options& settings, counters& values, arknet::io_pool& runtime)
        : settings_(settings)
        , values_(values)
        , contexts_(lanes(runtime))
        , endpoint_((std::min)(std::size_t{4096}, settings.maximum_buffer), settings.maximum_buffer, contexts_) {
        endpoint_.bind_accept([this](auto& session) noexcept {
            const auto error = arknet::get_last_error();
            values_.guard([&] {
                if (error)
                {
                    return;
                }
                ++values_.accepted;
                if (values_.admitted >= settings_.maximum)
                {
                    ++values_.rejected;
                    values_.fail("maximum admitted connection count exceeded");
                    session->stop();
                    return;
                }
                session->set_user_data(std::make_shared<server_connection>());
                session->set_disconnect_timeout(std::chrono::duration<double>(settings_.cleanup_seconds));
                session->set_silence_timeout(
                    std::chrono::duration<double>(settings_.deadline_seconds + settings_.cleanup_seconds));
                ++values_.admitted;
                values_.connected();
            });
        });
        endpoint_.bind_connect([this](auto& session) noexcept {
            values_.guard([&] {
                session->set_no_delay(true);
                if (arknet::get_last_error())
                {
                    throw std::runtime_error("setting TCP_NODELAY failed");
                }
            });
        });
        endpoint_.bind_recv([this](auto& session, std::string_view bytes) noexcept {
            values_.guard([&] { receive(session, bytes); });
        });
        endpoint_.bind_disconnect([this](auto& session) noexcept {
            auto state = session->template get_user_data<std::shared_ptr<server_connection>>();
            if (!state)
            {
                return;
            }
            --values_.active;
            ++values_.disconnected;
            if (!values_.closing && !state->finished)
            {
                ++values_.unexpected_disconnects;
                values_.fail("client disconnected before FIN acknowledgement");
            }
        });
    }

    ~server_run() {
        stop();
    }

    bool start() {
        return endpoint_.start(settings_.host, settings_.port, arknet::use_dgram);
    }

    std::uint16_t port() const {
        return endpoint_.get_listen_port();
    }

    void stop() {
        if (stopped_)
        {
            return;
        }
        values_.closing = true;
        endpoint_.stop();
        if (!endpoint_.is_stopped() || endpoint_.session_count())
        {
            values_.fail("server did not reach empty stopped state");
        }
        stopped_ = true;
    }

private:
    static std::vector<asio::io_context*> lanes(arknet::io_pool& runtime) {
        std::vector<asio::io_context*> contexts;
        for (std::size_t index = 0; index < runtime.size(); ++index)
        {
            contexts.push_back(&runtime.get_context(index));
        }
        return contexts;
    }

    void receive(std::shared_ptr<arknet::tcp_session>& session, std::string_view bytes) {
        auto state = session->get_user_data<std::shared_ptr<server_connection>>();
        frame_header header;
        if (!state || !connection_load::decode(bytes, header) || header.owner.run != settings_.run ||
            !header.owner.client || !header.owner.connection)
        {
            ++values_.content_errors;
            values_.fail("invalid request identity or body");
            return;
        }
        if (header.kind == frame_kind::hello && !state->hello &&
            header.sequence == (settings_.scenario == "traffic" ? 1 : 0))
        {
            {
                std::lock_guard lock(identities_mutex_);
                if (!identities_.emplace(header.owner.client, header.owner.connection).second)
                {
                    ++values_.content_errors;
                    values_.fail("duplicate client/connection identity");
                    return;
                }
            }
            state->owner = header.owner;
            state->hello = true;
            ++values_.ready;
            send_frame(*session, connection_load::make_frame(frame_kind::hello_ack, state->owner), values_, settings_);
        }
        else if (state->hello && header.owner == state->owner && header.kind == frame_kind::start && !state->started &&
                 !header.sequence)
        {
            state->started = true;
            std::int64_t unset{};
            values_.work_begin.compare_exchange_strong(unset, tick());
            ++values_.started;
            send_frame(*session, connection_load::make_frame(frame_kind::start_ack, state->owner), values_, settings_);
        }
        else if (state->started && header.owner == state->owner && header.kind == frame_kind::data &&
                 settings_.scenario == "traffic" && !state->finished && header.sequence == state->next_sequence &&
                 header.body_bytes == settings_.payload)
        {
            ++state->next_sequence;
            ++values_.received;
            send_frame(*session, std::string(bytes), values_, settings_, [this] { ++values_.sent; });
        }
        else if (state->started && header.owner == state->owner && header.kind == frame_kind::finish &&
                 !state->finished && header.sequence == state->next_sequence - 1)
        {
            state->finished = true;
            send_frame(*session, connection_load::make_frame(frame_kind::finish_ack, state->owner, header.sequence),
                values_, settings_, [this] {
                    values_.work_end = tick();
                    ++values_.finished;
                });
        }
        else
        {
            ++values_.sequence_errors;
            values_.fail("request kind, sequence or payload size mismatch");
        }
    }

    const options& settings_;
    counters& values_;
    std::vector<asio::io_context*> contexts_;
    arknet::tcp_server endpoint_;
    std::mutex identities_mutex_;
    std::set<std::pair<std::uint64_t, std::uint64_t>> identities_;
    bool stopped_{};
};

void result(const options& settings, const counters& values, std::string_view outcome, bool clean, double elapsed,
    double measurement, const connection_load::resource_snapshot& resources, std::uint64_t active_before_stop,
    std::size_t actual_port) {
    const auto work_seconds = values.work_end > values.work_begin && values.work_begin > 0
                                  ? double(values.work_end - values.work_begin) / 1e9
                                  : 0;
    std::cout
        << "{\"event\":\"result\",\"tool\":\"arknet_connection_benchmark\",\"format_version\":1,\"arknet_version\":"
        << quoted(ARKNET_VERSION_STRING) << ",\"role\":" << quoted(settings.role)
        << ",\"scenario\":" << quoted(settings.scenario) << ",\"outcome\":" << quoted(outcome)
        << ",\"clean_shutdown\":" << (clean ? "true" : "false")
        << ",\"run_id\":" << quoted(std::to_string(settings.run))
        << ",\"client_id\":" << quoted(std::to_string(settings.client)) << ",\"host\":" << quoted(settings.host)
        << ",\"port\":" << actual_port << ",\"requested_port\":" << settings.port
        << ",\"address_family\":" << quoted(settings.host.find(':') == std::string::npos ? "ipv4" : "ipv6")
        << ",\"target_connections\":" << (settings.role == "client" ? settings.connections : 0)
        << ",\"expected_connections\":" << settings.expected << ",\"io_contexts\":" << settings.threads
        << ",\"workers_per_context\":1,\"io_threads\":" << settings.threads << ",\"elapsed_s\":" << elapsed
        << ",\"requested_hold_s\":" << settings.hold_seconds << ",\"hold_elapsed_s\":" << measurement
        << ",\"traffic_and_drain_elapsed_s\":" << work_seconds
        << ",\"echo_messages_per_second\":" << (work_seconds > 0 ? double(values.received) / work_seconds : 0)
        << ",\"echo_payload_bytes_per_second\":"
        << (work_seconds > 0 ? double(values.received) * settings.payload / work_seconds : 0)
        << ",\"active_before_stop\":" << active_before_stop << ",\"attempted\":" << values.attempted
        << ",\"established\":" << values.established << ",\"accepted\":" << values.accepted
        << ",\"admitted\":" << values.admitted << ",\"ready\":" << values.ready << ",\"started\":" << values.started
        << ",\"finished\":" << values.finished << ",\"active\":" << values.active << ",\"peak_active\":" << values.peak
        << ",\"disconnected\":" << values.disconnected << ",\"connect_failures\":" << values.connect_failures
        << ",\"timeouts\":" << values.timeouts << ",\"unexpected_disconnects\":" << values.unexpected_disconnects
        << ",\"content_errors\":" << values.content_errors << ",\"sequence_errors\":" << values.sequence_errors
        << ",\"send_errors\":" << values.send_errors << ",\"resource_rejections\":" << values.rejected
        << ",\"data_sent\":" << values.sent << ",\"data_received\":" << values.received
        << ",\"outstanding_requests\":" << values.outstanding << ",\"pending_sends\":" << values.pending_sends
        << ",\"pending_send_bytes\":" << values.pending_bytes
        << ",\"shutdown_cancelled_sends\":" << values.shutdown_cancelled_sends
        << ",\"connect_rate_limit\":" << settings.rate << ",\"connect_parallel_limit\":" << settings.parallel
        << ",\"max_connections\":" << settings.maximum << ",\"payload_bytes\":" << settings.payload
        << ",\"window\":" << settings.window << ",\"max_buffer_bytes\":" << settings.maximum_buffer
        << ",\"max_inflight_bytes\":" << settings.inflight_limit
        << ",\"buffer_budget_bytes\":" << settings.buffer_budget << ",\"sample_interval_ms\":" << settings.sample_ms
        << ",\"peak_pending_send_bytes\":" << values.peak_pending_bytes;
    connection_load::write_resource_fields(std::cout, resources, true);
    std::cout << ",\"first_error\":" << quoted(values.first_error)
              << ",\"latency_observations\":" << values.latency.total();
    for (const auto percent : {50, 95, 99})
    {
        std::cout << ",\"rtt_p" << percent << "_upper_us\":";
        if (values.latency.total())
        {
            std::cout << values.latency.percentile(percent);
        }
        else
        {
            std::cout << "null";
        }
    }
    std::cout << ",\"rtt_buckets\":[";
    for (std::size_t index = 0; index < connection_load::latency_distribution::bucket_count; ++index)
    {
        if (index)
        {
            std::cout << ',';
        }
        std::cout << values.latency.count(index);
    }
    std::cout << "]}\n" << std::flush;
}

int run(const options& settings) {
    const auto began = clock_type::now();
    const auto deadline = began + std::chrono::duration_cast<clock_type::duration>(
                                      std::chrono::duration<double>(settings.deadline_seconds));
    cleanup_watchdog watchdog(deadline + std::chrono::duration_cast<clock_type::duration>(
                                             std::chrono::duration<double>(settings.cleanup_seconds)));
    connection_load::resource_observer overall_resources(began);
    counters values;
    observation samples(settings, values, began);
    arknet::io_pool runtime(settings.threads);
    if (!runtime.start())
    {
        throw std::runtime_error("IO pool start failed");
    }
    double measurement{};
    std::uint64_t active_before_stop{};
    std::size_t actual_port = settings.port;
    std::string outcome = "failed";
    auto wait = [&] {
        if (interrupted.load(std::memory_order_relaxed))
        {
            values.fail("interrupted by signal");
        }
        samples.sample();
        std::this_thread::sleep_for(1ms);
    };
    if (settings.role == "server")
    {
        server_run server(settings, values, runtime);
        if (!server.start())
        {
            throw std::runtime_error("listen: " + arknet::get_last_error().message());
        }
        actual_port = server.port();
        std::cout << "{\"event\":\"ready\",\"role\":\"server\",\"host\":" << quoted(settings.host)
                  << ",\"port\":" << actual_port << ",\"run_id\":" << quoted(std::to_string(settings.run)) << "}\n"
                  << std::flush;
        while (!values.failed && clock_type::now() < deadline)
        {
            if (settings.expected && values.finished == settings.expected && values.disconnected == settings.expected &&
                values.admitted == settings.expected && !values.active && !values.pending_sends)
            {
                break;
            }
            wait();
        }
        if (!values.failed)
        {
            if (!settings.expected)
            {
                outcome = "observation_complete";
            }
            else if (values.finished == settings.expected && values.disconnected == settings.expected &&
                     values.admitted == settings.expected && values.ready == settings.expected &&
                     values.started == settings.expected && !values.active && !values.pending_sends)
            {
                outcome = "target_complete";
            }
            else
            {
                ++values.timeouts;
                values.fail("server target deadline exceeded");
            }
        }
        active_before_stop = values.active;
        server.stop();
    }
    else
    {
        client_run client(settings, values, runtime);
        auto next_launch = began;
        while (!values.failed && values.ready < settings.connections && clock_type::now() < deadline)
        {
            if (values.attempted < settings.connections && values.connecting < settings.parallel &&
                clock_type::now() >= next_launch)
            {
                client.launch();
                next_launch = clock_type::now() + std::chrono::nanoseconds(1000000000 / settings.rate);
            }
            client.check_timeouts();
            wait();
        }
        if (!values.failed && values.ready == settings.connections)
        {
            client.start();
            while (!values.failed && values.started < settings.connections && clock_type::now() < deadline)
            {
                client.check_timeouts();
                wait();
            }
        }
        if (!values.failed && values.started == settings.connections)
        {
            const auto started = clock_type::now();
            const auto until = started + std::chrono::duration_cast<clock_type::duration>(
                                             std::chrono::duration<double>(settings.hold_seconds));
            client.traffic();
            while (!values.failed && clock_type::now() < until && clock_type::now() < deadline)
            {
                client.check_timeouts();
                wait();
            }
            measurement = std::chrono::duration<double>(clock_type::now() - started).count();
            if (!values.failed && clock_type::now() < deadline)
            {
                client.finish();
                while (!values.failed && (values.finished < settings.connections || values.pending_sends) &&
                       clock_type::now() < deadline)
                {
                    client.check_timeouts();
                    wait();
                }
            }
        }
        if (!values.failed)
        {
            if (values.finished == settings.connections && !values.outstanding && !values.pending_sends &&
                (settings.scenario == "idle" || (values.received > 0 && values.received == values.sent)))
            {
                outcome = "target_complete";
            }
            else
            {
                ++values.timeouts;
                values.fail("client target deadline exceeded");
            }
        }
        active_before_stop = values.active;
        client.stop();
    }
    runtime.stop();
    const bool clean = runtime.stopped() && !values.active && !values.pending_sends && !values.pending_bytes;
    if (!clean)
    {
        values.fail("cleanup counters did not reach terminal state");
    }
    if (values.failed)
    {
        outcome = "failed";
    }
    const auto resources = overall_resources.sample();
    const auto elapsed = std::chrono::duration<double>(clock_type::now() - began).count();
    result(settings, values, outcome, clean, elapsed, measurement, resources, active_before_stop, actual_port);
    return values.failed ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    try
    {
        std::signal(SIGINT, request_interrupt);
        std::signal(SIGTERM, request_interrupt);
        std::cout << std::setprecision(9);
        return run(arguments(argc, argv));
    }
    catch (const std::exception& error)
    {
        std::cerr << "{\"event\":\"error\",\"reason\":" << quoted(error.what()) << "}\n";
        return 2;
    }
}
