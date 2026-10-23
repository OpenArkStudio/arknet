// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
// Native callback control: deliberately no arknet headers or runtime objects.
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(__APPLE__)
#include <netinet/tcp.h>
#include <sys/socket.h>

#endif

#if ARKNET_CONTROL_BOOST
#include <boost/asio.hpp>
#include <boost/version.hpp>

static_assert(BOOST_VERSION >= 109000, "native control requires Boost 1.90.0 or newer");
namespace asio = boost::asio;
using error_code = boost::system::error_code;
constexpr auto dependency = "Boost.Asio";
constexpr auto dependency_version = BOOST_VERSION;
#else
#include <asio.hpp>

static_assert(ASIO_VERSION >= 103800, "native control requires Standalone Asio 1.38.0 or newer");
using error_code = asio::error_code;
constexpr auto dependency = "Standalone Asio";
constexpr auto dependency_version = ASIO_VERSION;
#endif

#include "connections_kernel_samples.hpp"
#include "connections_protocol.hpp"
#include "connections_resources.hpp"
#include "connections_socket_trace.hpp"
#include "connections_trace.hpp"

namespace {

using clock_type = std::chrono::steady_clock;
using tcp = asio::ip::tcp;
using connection_load::frame_kind;
using connection_load::header_bytes;
using connection_load::identity;

struct options
{
    std::string role, scenario = "idle", host;
    std::uint64_t run{}, client = 1;
    std::size_t port{}, connections = 1, expected{}, maximum = 5000;
    std::size_t rate = 100, parallel = 32, payload = 1024, window = 1;
    std::size_t connect_ms = 5000, request_ms = 5000, inflight = 20000;
    std::size_t inflight_bytes = 64 * 1024 * 1024, budget = 512 * 1024 * 1024;
    double hold = 1, deadline = 30, cleanup = 5;
    bool operation_trace{}, socket_operation_trace{}, tcp_kernel_samples{};
};

std::uint64_t integer(std::string_view text) {
    std::uint64_t value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        throw std::invalid_argument("expected an unsigned decimal integer");
    }
    return value;
}

double seconds(const std::string& text) {
    double value{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
    {
        throw std::invalid_argument("expected finite decimal seconds");
    }
    return value;
}

void validate(const options& o) {
    if (o.tcp_kernel_samples && !o.operation_trace)
    {
        throw std::invalid_argument("tcp-kernel-samples requires operation-trace on");
    }
    if (o.socket_operation_trace && !o.operation_trace)
    {
        throw std::invalid_argument("socket-operation-trace requires operation-trace on");
    }
    if ((o.role != "server" && o.role != "client") || (o.scenario != "idle" && o.scenario != "traffic") || !o.run ||
        !o.client || o.host.empty() || o.port > 65535 || (o.role == "client" && !o.port))
    {
        throw std::invalid_argument("specify role, listen/target, port and nonzero run/client IDs");
    }
    error_code error;
    const auto address = asio::ip::make_address(o.host, error);
    if (error || address.is_multicast() || (o.role == "client" && address.is_unspecified()))
    {
        throw std::invalid_argument("address must be numeric unicast IPv4 or IPv6");
    }
    if (!o.maximum || o.maximum > 200000 || !o.connections || o.connections > o.maximum || !o.expected ||
        o.expected > o.maximum || !o.rate || o.rate > 1000000 || !o.parallel || o.parallel > 4096 || !o.payload ||
        o.payload > 4023 || !o.window || o.window > 1024 || !o.inflight || o.inflight > 100000000 ||
        o.inflight_bytes < header_bytes + o.payload || o.inflight_bytes > 1024ULL * 1024 * 1024 ||
        o.budget > 16ULL * 1024 * 1024 * 1024 || !o.connect_ms || o.connect_ms > 3600000 || !o.request_ms ||
        o.request_ms > 3600000 || !std::isfinite(o.hold) || o.hold <= 0 || !std::isfinite(o.deadline) ||
        o.deadline <= o.hold || o.deadline > 86400 || !std::isfinite(o.cleanup) || o.cleanup < 1 || o.cleanup > 60)
    {
        throw std::invalid_argument("argument out of range (server requires a nonzero expected-connections)");
    }
    const auto count = o.role == "client" ? o.connections : o.maximum;
    const auto slots = std::uint64_t(count) * o.window;
    if (o.scenario == "traffic" && (slots > o.inflight || slots * (header_bytes + o.payload) > o.inflight_bytes))
    {
        throw std::invalid_argument("declared connection windows exceed data inflight count/byte limits");
    }
    // Same declared receive/send/timestamp reservation as the arknet benchmark.
    const auto reserved = std::uint64_t(count) * 4096 + o.inflight_bytes +
                          (o.role == "client" && o.scenario == "traffic" ? slots * sizeof(clock_type::time_point) : 0);
    if (reserved > o.budget)
    {
        throw std::invalid_argument("declared buffer reservation exceeds buffer-budget-bytes");
    }
}

void help() {
    std::cout << "arknet_asio_connection_control --role server|client --scenario idle|traffic\n"
                 "  server: --listen NUMERIC_IP --port PORT --run-id ID --expected-connections COUNT\n"
                 "  client: --target NUMERIC_IP --port PORT --run-id ID --client-id ID --connections COUNT\n"
                 "  --connect-rate COUNT_PER_SECOND --connect-parallel COUNT --connect-timeout-ms MS\n"
                 "  --hold-seconds SECONDS --deadline-seconds SECONDS --cleanup-seconds SECONDS\n"
                 "  --request-timeout-ms MS --io-threads 1 --payload BYTES --window COUNT\n"
                 "  --max-connections COUNT --max-buffer-bytes 4096 --max-inflight COUNT\n"
                 "  --max-inflight-bytes BYTES --buffer-budget-bytes BYTES\n"
                 "  --operation-trace off|on (default off; bounded composed-operation diagnostics)\n"
                 "  --socket-operation-trace off|on (default off; requires operation-trace on)\n"
                 "  --tcp-kernel-samples off|on (default off; requires operation-trace on; Apple TCP info)\n"
                 "  --self-test (unit checks only; no sockets)\n"
                 "One native Asio context/worker, session strands, TCP_NODELAY, 4096B read_until cap.\n"
                 "Defaults permit 5000 active connections, 32 connecting and 20000 data inflight.\n"
                 "Server port 0 reports its selected port; expected count must be nonzero.\n"
                 "Unsupported options (including sample-ms and multiple workers) are rejected.\n";
}

options arguments(int argc, char** argv) {
    options o;
    std::set<std::string> seen;
    bool port_set = false;
    std::string address_option;
    for (int index = 1; index < argc; ++index)
    {
        const std::string key = argv[index];
        if (!seen.insert(key).second || index + 1 == argc)
        {
            throw std::invalid_argument("duplicate option or missing value: " + key);
        }
        const std::string value = argv[++index];
        if (key == "--role")
        {
            o.role = value;
        }
        else if (key == "--scenario")
        {
            o.scenario = value;
        }
        else if (key == "--listen" || key == "--target")
        {
            o.host = value;
            address_option = key;
        }
        else if (key == "--port")
        {
            o.port = integer(value);
            port_set = true;
        }
        else if (key == "--run-id")
        {
            o.run = integer(value);
        }
        else if (key == "--client-id")
        {
            o.client = integer(value);
        }
        else if (key == "--connections")
        {
            o.connections = integer(value);
        }
        else if (key == "--expected-connections")
        {
            o.expected = integer(value);
        }
        else if (key == "--max-connections")
        {
            o.maximum = integer(value);
        }
        else if (key == "--connect-rate")
        {
            o.rate = integer(value);
        }
        else if (key == "--connect-parallel")
        {
            o.parallel = integer(value);
        }
        else if (key == "--payload")
        {
            o.payload = integer(value);
        }
        else if (key == "--window")
        {
            o.window = integer(value);
        }
        else if (key == "--connect-timeout-ms")
        {
            o.connect_ms = integer(value);
        }
        else if (key == "--request-timeout-ms")
        {
            o.request_ms = integer(value);
        }
        else if (key == "--max-inflight")
        {
            o.inflight = integer(value);
        }
        else if (key == "--max-inflight-bytes")
        {
            o.inflight_bytes = integer(value);
        }
        else if (key == "--buffer-budget-bytes")
        {
            o.budget = integer(value);
        }
        else if (key == "--hold-seconds")
        {
            o.hold = seconds(value);
        }
        else if (key == "--deadline-seconds")
        {
            o.deadline = seconds(value);
        }
        else if (key == "--cleanup-seconds")
        {
            o.cleanup = seconds(value);
        }
        else if (key == "--operation-trace" && (value == "on" || value == "off"))
        {
            o.operation_trace = value == "on";
        }
        else if (key == "--socket-operation-trace" && (value == "on" || value == "off"))
        {
            o.socket_operation_trace = value == "on";
        }
        else if (key == "--tcp-kernel-samples" && (value == "on" || value == "off"))
        {
            o.tcp_kernel_samples = value == "on";
        }
        else if (key == "--io-threads" && integer(value) == 1)
        {
        }
        else if (key == "--max-buffer-bytes" && integer(value) == 4096)
        {
        }
        else
        {
            throw std::invalid_argument("unsupported option or value: " + key);
        }
    }
    if (!port_set || address_option != (o.role == "server" ? "--listen" : "--target") ||
        (seen.contains("--listen") && seen.contains("--target")) ||
        (o.role == "client" && seen.contains("--expected-connections")) ||
        (o.role == "server" && (seen.contains("--connections") || seen.contains("--client-id"))))
    {
        throw std::invalid_argument("missing port or role-inappropriate option");
    }
    if (o.role == "client")
    {
        o.expected = o.connections;
    }
    validate(o);
    return o;
}

// Independent dgram decoder: inspect a bounded prefix first, then require exactly
// the declared payload before accepting a frame. It never invokes arknet's matcher or receive helpers.
struct dgram_match
{
    using result_type = std::pair<asio::buffers_iterator<asio::streambuf::const_buffers_type>, bool>;

    template<class Iterator>
    std::pair<Iterator, bool> operator()(Iterator begin, Iterator end) const {
        auto cursor = begin;
        if (cursor == end)
        {
            return {begin, false};
        }
        const auto marker = static_cast<unsigned char>(*cursor++);
        const unsigned width = marker == 255 ? 8 : marker == 254 ? 2 : 0;
        std::uint64_t size = width ? 0 : marker;
        for (unsigned byte = 0; byte < width; ++byte)
        {
            if (cursor == end)
            {
                return {begin, false};
            }
            size |= std::uint64_t(static_cast<unsigned char>(*cursor++)) << (byte * 8);
        }
        if ((width == 2 && size < 254) || (width == 8 && size < 65536) || size > 4096 - 1 - width)
        {
            return {begin, true};
        }
        if (static_cast<std::uint64_t>(std::distance(cursor, end)) < size)
        {
            return {begin, false};
        }
        std::advance(cursor, static_cast<typename std::iterator_traits<Iterator>::difference_type>(size));
        return {cursor, true};
    }
};

struct wire_frame
{
    explicit wire_frame(std::string value)
        : body(std::move(value)) {
        const auto size = body.size();
        prefix_size = size < 254 ? 1 : size <= 65535 ? 3 : 9;
        prefix[0] = static_cast<char>(prefix_size == 1 ? size : prefix_size == 3 ? 254 : 255);
        for (std::size_t byte = 1; byte < prefix_size; ++byte)
        {
            prefix[byte] = static_cast<char>(std::uint64_t(size) >> ((byte - 1) * 8));
        }
    }

    std::array<char, 9> prefix{};
    std::size_t prefix_size{};
    std::string body;

    bool is_data() const {
        return connection_load::read_integer(body, 8) == static_cast<std::uint64_t>(frame_kind::data);
    }
};

// Shared by runtime and self-test; rejects a wrong owner, phase or sequence.
struct protocol_state
{
    identity owner{};
    bool hello{}, started{}, finish_sent{}, finished{};
    std::uint64_t next_send = 1, next_receive = 1;

    bool accept(const connection_load::frame_header& h, const options& o, bool server) {
        if (server && h.kind == frame_kind::hello && !hello && h.owner.run == o.run && h.owner.client &&
            h.owner.connection && h.sequence == (o.scenario == "traffic" ? 1 : 0))
        {
            owner = h.owner;
            hello = true;
            return true;
        }
        if (h.owner != owner)
        {
            return false;
        }
        if (!server && h.kind == frame_kind::hello_ack && !hello && !h.sequence)
        {
            hello = true;
            return true;
        }
        if (h.kind == (server ? frame_kind::start : frame_kind::start_ack) && hello && !started && !h.sequence)
        {
            started = true;
            return true;
        }
        if (h.kind == frame_kind::data && started && !finished && o.scenario == "traffic" &&
            h.body_bytes == o.payload && h.sequence == next_receive && (server || h.sequence < next_send))
        {
            ++next_receive;
            return true;
        }
        if (h.kind == (server ? frame_kind::finish : frame_kind::finish_ack) && started && !finished &&
            (server || finish_sent) && h.sequence == (server ? next_receive : next_send) - 1)
        {
            finished = true;
            return true;
        }
        return false;
    }
};

struct application;

struct session : std::enable_shared_from_this<session>
{
    session(application& owner, asio::io_context& context);
    void read();
    void send(std::string body);
    void write();
    void close();
    void fill();
    void finish();
    void receive(std::string_view bytes);
    void diagnose();
    void enable_trace();
    void trace(connection_load::operation_phase phase, std::uint64_t bytes, std::uint64_t sequence, int error = 0);

    application& app;
    asio::strand<asio::io_context::executor_type> strand;
    tcp::socket socket;
    asio::streambuf input{4096};
    protocol_state state;
    std::deque<std::shared_ptr<wire_frame>> queued;
    std::shared_ptr<wire_frame> writing;
    std::vector<clock_type::time_point> sent_at;
    std::atomic<clock_type::time_point> pending_since{}, connect_since{};
    std::atomic<bool> connecting{};
    bool active{}, closed{}, peer_eof{};
    bool read_pending{};
    connection_load::operation_ring* operations{};
    std::unique_ptr<connection_load::socket_trace_stream<tcp::socket>> socket_trace;
    std::uint64_t read_completions{}, write_completions{}, last_write_kind{}, last_write_sequence{};
    clock_type::time_point last_read_at{}, last_write_at{};
};

struct application
{
    explicit application(options value)
        : o(std::move(value))
        , operations(o.operation_trace, o.role == "client" ? o.connections : o.expected, o.socket_operation_trace,
              sizeof(connection_load::socket_trace_stream<tcp::socket>) +
                  2 * connection_load::socket_handler_reservation)
        , kernel_samples(o.tcp_kernel_samples)
        , acceptor(io)
        , cleanup(io)
        , signals(io, SIGINT, SIGTERM) {}

    void run();
    void accept();
    void launch();
    void poll();
    void sample();
    void sample_kernel();
    void fail(const std::string& scope, const std::string& reason);
    void stop();
    void drained();
    void result() const;
    void trace_failure();

    std::int64_t elapsed_us() const {
        return std::chrono::duration_cast<std::chrono::microseconds>(clock_type::now() - began).count();
    }

    options o;
    connection_load::operation_recorder operations;
    connection_load::tcp_kernel_samples kernel_samples;
    bool trace_emitted{};
    asio::io_context io{1};
    tcp::acceptor acceptor;
    asio::steady_timer cleanup;
    asio::executor_work_guard<asio::io_context::executor_type> work{asio::make_work_guard(io)};
    asio::signal_set signals;
    std::vector<std::shared_ptr<session>> sessions; // IO-thread ownership.
    std::vector<std::shared_ptr<session>> clients;  // Owner-thread ownership.
    std::set<std::pair<std::uint64_t, std::uint64_t>> identities;
    std::atomic<std::uint64_t> attempted{}, accepted{}, established{}, connecting{}, active{}, peak_active{},
        peak_connecting{};
    std::atomic<std::uint64_t> ready{}, started{}, finished{}, disconnected{}, data_sent{}, data_received{},
        outstanding{};
    std::atomic<std::uint64_t> pending_writes{}, pending_bytes{}, pending_data_writes{}, callbacks{}, data_bytes{},
        peak_inflight{}, peak_bytes{};
    std::atomic<std::uint64_t> write_errors{}, state_errors{}, connect_errors{}, timeouts{}, rejected{},
        cancelled_writes{};
    std::atomic<bool> failed{}, stopping{}, traffic{}, draining{};
    bool cleanup_started{}, cleanup_timeout{}, starting{};
    std::string failure;
    clock_type::time_point began = clock_type::now(), next_launch = began, next_timeout_scan = began, until{},
                           stopped_at{}, drained_at{};
    clock_type::time_point sampled = began;
    connection_load::resource_observer resources{began};
};

session::session(application& owner, asio::io_context& context)
    : app(owner)
    , strand(asio::make_strand(context))
    , socket(strand) {
    // Match the arknet driver's initial receive reservation as well as its cap.
    input.prepare(4096);
}

void application::fail(const std::string& scope, const std::string& reason) {
    if (!failed.exchange(true))
    {
        failure = scope + ": " + reason;
    }
}

void application::drained() {
    if (cleanup_started && !callbacks && !pending_writes && !pending_bytes && !pending_data_writes && !active &&
        !connecting)
    {
        drained_at = clock_type::now();
        cleanup.cancel();
        work.reset();
    }
}

void session::close() {
    if (closed)
    {
        return;
    }
    closed = true;
    error_code ignored;
    socket.cancel(ignored);
    socket.close(ignored);
    if (active)
    {
        active = false;
        --app.active;
        ++app.disconnected;
    }
    for (const auto& value : queued)
    {
        --app.pending_writes;
        app.pending_bytes -= value->body.size();
        if (value->is_data())
        {
            --app.pending_data_writes;
        }
        ++app.cancelled_writes;
    }
    queued.clear();
}

void application::stop() {
    // Only the owner thread calls stop, after admission has quiesced.
    if (stopping.exchange(true))
    {
        return;
    }
    stopped_at = clock_type::now();
    asio::post(io, [this] {
        cleanup_started = true;
        if (failed)
        {
            if (operations.enabled())
            {
                trace_failure();
            }
            std::size_t incomplete{}, emitted{};
            for (const auto& value : sessions)
            {
                if (!value->state.finished || value->writing || !value->queued.empty())
                {
                    ++incomplete;
                    if (emitted < 16)
                    {
                        value->diagnose();
                        ++emitted;
                    }
                }
            }
            std::cout << "{\"event\":\"diagnostic_summary\",\"role\":" << std::quoted(o.role)
                      << ",\"incomplete_sessions\":" << incomplete << ",\"emitted\":" << emitted
                      << ",\"omitted\":" << incomplete - emitted << "}\n"
                      << std::flush;
        }
        error_code ignored;
        acceptor.close(ignored);
        signals.cancel();
        cleanup.expires_after(
            std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(o.cleanup)));
        cleanup.async_wait([this](error_code error) {
            if (!error)
            {
                cleanup_timeout = true;
                fail("cleanup", "socket callbacks did not drain");
                io.stop();
            }
        });
        for (const auto& value : sessions)
        {
            asio::dispatch(value->strand, [value] {
                value->close();
                value->app.drained();
            });
        }
        drained();
    });
}

void session::send(std::string body) {
    if (closed || app.stopping || app.failed)
    {
        return;
    }
    const bool data = connection_load::read_integer(body, 8) == static_cast<std::uint64_t>(frame_kind::data);
    if ((data && app.pending_data_writes >= app.o.inflight) || body.size() > app.o.inflight_bytes - app.pending_bytes)
    {
        ++app.rejected;
        app.fail("write admission", "data count or send-byte limit exceeded");
        return;
    }
    const auto value = std::make_shared<wire_frame>(std::move(body));
    queued.push_back(value);
    ++app.pending_writes;
    if (data)
    {
        ++app.pending_data_writes;
    }
    app.pending_bytes += value->body.size();
    app.peak_bytes = (std::max)(app.peak_bytes.load(), app.pending_bytes.load());
    write();
}

void session::write() {
    if (writing || queued.empty() || closed)
    {
        return;
    }
    writing = queued.front();
    queued.pop_front();
    const auto value = writing;
    const std::array<asio::const_buffer, 2> buffers{
        asio::buffer(value->prefix.data(), value->prefix_size), asio::buffer(value->body)};
    ++app.callbacks;
    if (operations)
    {
        trace(connection_load::operation_phase::write_start, value->prefix_size + value->body.size(),
            connection_load::read_integer(value->body, 40));
    }
    auto completion =
        asio::bind_executor(strand, [self = shared_from_this(), value](error_code error, std::size_t size) {
            auto& a = self->app;
            if (self->operations)
            {
                self->trace(connection_load::operation_phase::write_complete, size,
                    connection_load::read_integer(value->body, 40), error.value());
            }
            --a.callbacks;
            --a.pending_writes;
            a.pending_bytes -= value->body.size();
            if (value->is_data())
            {
                --a.pending_data_writes;
            }
            self->writing.reset();
            if (error || size != value->body.size() + value->prefix_size)
            {
                if (a.stopping && error == asio::error::operation_aborted)
                {
                    ++a.cancelled_writes;
                }
                else
                {
                    ++a.write_errors;
                    a.fail("write callback", error ? error.message() : "short async_write");
                }
            }
            else
            {
                const auto kind = static_cast<frame_kind>(connection_load::read_integer(value->body, 8));
                ++self->write_completions;
                self->last_write_at = clock_type::now();
                self->last_write_kind = static_cast<std::uint64_t>(kind);
                self->last_write_sequence = connection_load::read_integer(value->body, 40);
                if (kind == frame_kind::data)
                {
                    ++a.data_sent;
                }
                if (a.o.role == "server" && kind == frame_kind::finish_ack)
                {
                    ++a.finished;
                }
                self->write();
                if (self->peer_eof && !self->writing && self->queued.empty())
                {
                    self->close();
                }
            }
            a.drained();
        });
    if (socket_trace)
    {
        socket_trace->write_sequence(connection_load::read_integer(value->body, 40));
        asio::async_write(*socket_trace, buffers, std::move(completion));
    }
    else
    {
        asio::async_write(socket, buffers, std::move(completion));
    }
}

void session::read() {
    if (closed || app.stopping || app.failed)
    {
        return;
    }
    ++app.callbacks;
    read_pending = true;
    trace(connection_load::operation_phase::read_start, 0, state.next_receive);
    auto completion = asio::bind_executor(strand, [self = shared_from_this()](error_code error, std::size_t size) {
        auto& a = self->app;
        self->trace(connection_load::operation_phase::read_complete, size, self->state.next_receive, error.value());
        --a.callbacks;
        self->read_pending = false;
        ++self->read_completions;
        self->last_read_at = clock_type::now();
        if (error)
        {
            if (!a.stopping && error == asio::error::eof && self->state.finished)
            {
                self->peer_eof = true;
                if (!self->writing && self->queued.empty())
                {
                    self->close();
                }
            }
            else if (!a.stopping)
            {
                ++a.state_errors;
                a.fail("read callback", error.message());
            }
        }
        else if (!a.stopping)
        {
            if (!size)
            {
                ++a.state_errors;
                a.fail("read frame", "invalid or oversized dgram prefix");
            }
            else
            {
                // basic_streambuf supplies one contiguous input sequence.
                // Borrow it only during receive; queued writes own their copies.
                const auto buffers = self->input.data();
                const auto buffer = *asio::buffer_sequence_begin(buffers);
                const auto* bytes = static_cast<const char*>(buffer.data());
                const auto marker = static_cast<unsigned char>(*bytes);
                const std::size_t prefix = marker < 254 ? 1 : marker == 254 ? 3 : 9;
                self->receive(std::string_view(bytes + prefix, size - prefix));
                self->input.consume(size);
                self->read();
            }
        }
        a.drained();
    });
    if (socket_trace)
    {
        socket_trace->read_sequence(state.next_receive);
        asio::async_read_until(*socket_trace, input, dgram_match{}, std::move(completion));
    }
    else
    {
        asio::async_read_until(socket, input, dgram_match{}, std::move(completion));
    }
}

void session::enable_trace() {
    connection_load::operation_ring* socket_operations{};
    operations = app.operations.acquire(&socket_operations);
    if (socket_operations)
    {
        socket_trace =
            std::make_unique<connection_load::socket_trace_stream<tcp::socket>>(socket, *socket_operations, app.began);
    }
}

void session::trace(connection_load::operation_phase phase, std::uint64_t bytes, std::uint64_t sequence, int error) {
    if (operations)
    {
        operations->record(phase, app.elapsed_us(), bytes, sequence, error);
    }
}

void application::trace_failure() {
    // Single IO worker, before any socket cancellation. Freeze prevents cleanup
    // callbacks from replacing the final pre-cancel operation history.
    operations.freeze();
    kernel_samples.freeze();
    connection_load::trace_output output;
    std::size_t incomplete{}, emitted{}, histories_incomplete{}, socket_histories_incomplete{},
        kernel_histories_available{}, kernel_histories_emitted{};
    for (const auto& value : sessions)
    {
        if (!value->state.finished || value->writing || !value->queued.empty())
        {
            ++incomplete;
            if (emitted < 16)
            {
                const bool retained = value->operations && value->operations->history_complete();
                const bool written = output.append([&](std::ostream& out) {
                    out << "{\"event\":\"operation_trace\",\"schema\":1,\"role\":" << std::quoted(o.role)
                        << ",\"run_id\":" << o.run << ",\"client_id\":" << value->state.owner.client
                        << ",\"connection_id\":" << value->state.owner.connection
                        << ",\"snapshot_phase\":\"before_socket_cancel\",\"operation_scope\":"
                        << (o.socket_operation_trace ? "\"composed_and_socket\"" : "\"composed_only\"")
                        << ",\"trace\":";
                    if (value->operations)
                    {
                        out << '{';
                        value->operations->write_json(out);
                        out << '}';
                    }
                    else
                    {
                        out << "null";
                    }
                    if (o.socket_operation_trace)
                    {
                        out << ",\"socket_trace\":";
                        if (value->socket_trace)
                        {
                            out << '{';
                            value->socket_trace->ring().write_json(out);
                            out << '}';
                        }
                        else
                        {
                            out << "null";
                        }
                    }
                    out << '}';
                });
                if (written)
                {
                    ++emitted;
                    histories_incomplete += !retained;
                    socket_histories_incomplete +=
                        o.socket_operation_trace &&
                        (!value->socket_trace || !value->socket_trace->ring().history_complete());
                }
            }
            if (o.tcp_kernel_samples && kernel_samples.contains(value->state.owner))
            {
                ++kernel_histories_available;
                kernel_histories_emitted += output.append([&](std::ostream& out) {
                    out << "{\"event\":\"tcp_kernel_sample_history\",\"schema\":1,\"role\":" << std::quoted(o.role)
                        << ",\"run_id\":" << o.run << ",\"client_id\":" << value->state.owner.client
                        << ",\"connection_id\":" << value->state.owner.connection << ',';
                    kernel_samples.write_history(out, value->state.owner);
                    out << '}';
                });
            }
        }
    }
    const auto summary = output.append(
        [&](std::ostream& out) {
            out << "{\"event\":\"operation_trace_summary\",\"schema\":1,\"role\":" << std::quoted(o.role)
                << ",\"run_id\":" << o.run
                << ",\"snapshot_phase\":\"before_socket_cancel\",\"incomplete_sessions\":" << incomplete
                << ",\"emitted_sessions\":" << emitted << ",\"omitted_incomplete_sessions\":" << incomplete - emitted
                << ",\"incomplete_emitted_histories\":" << histories_incomplete
                << ",\"incomplete_socket_histories\":" << socket_histories_incomplete
                << ",\"output_truncated\":" << (output.truncated() ? "true" : "false") << ',';
            operations.write_json(out);
            if (o.tcp_kernel_samples)
            {
                out << ",\"tcp_kernel_histories_emitted\":" << kernel_histories_emitted
                    << ",\"tcp_kernel_histories_omitted\":" << kernel_histories_available - kernel_histories_emitted
                    << ",\"tcp_kernel_samples\":";
                kernel_samples.write_summary(out);
            }
            out << '}';
        },
        true);
    std::cout << output.text();
    trace_emitted = summary;
}

void session::diagnose() {
    // Called only on the single IO worker, before stop cancels any socket.
    // Diagnostic queries neither consume bytes nor alter workload counters.
    const auto now = clock_type::now();
    auto age = [now](clock_type::time_point value) {
        return value == clock_type::time_point{} ? -1.0
                                                 : std::chrono::duration<double, std::milli>(now - value).count();
    };
    auto endpoint_name = [](tcp::endpoint value, const error_code& error) {
        return error ? std::string{} : value.address().to_string() + ":" + std::to_string(value.port());
    };
    error_code local_error, remote_error, available_error, receive_error, send_error;
    const auto local = socket.local_endpoint(local_error);
    const auto remote = socket.remote_endpoint(remote_error);
    const auto available = socket.available(available_error);
    asio::socket_base::receive_buffer_size receive_buffer;
    asio::socket_base::send_buffer_size send_buffer;
    socket.get_option(receive_buffer, receive_error);
    socket.get_option(send_buffer, send_error);
    const auto buffers = input.data();
    const auto begin = asio::buffers_begin(buffers), end = asio::buffers_end(buffers);
    const auto match = dgram_match{}(begin, end);
    std::cout << "{\"event\":\"connection_diagnostic\",\"snapshot_phase\":\"before_socket_cancel\",\"role\":"
              << std::quoted(app.o.role) << ",\"run_id\":" << app.o.run << ",\"client_id\":" << state.owner.client
              << ",\"connection_id\":" << state.owner.connection
              << ",\"local_endpoint\":" << std::quoted(endpoint_name(local, local_error))
              << ",\"remote_endpoint\":" << std::quoted(endpoint_name(remote, remote_error))
              << ",\"hello\":" << (state.hello ? "true" : "false")
              << ",\"started\":" << (state.started ? "true" : "false")
              << ",\"finish_sent\":" << (state.finish_sent ? "true" : "false")
              << ",\"finished\":" << (state.finished ? "true" : "false")
              << ",\"closed\":" << (closed ? "true" : "false")
              << ",\"read_pending\":" << (read_pending ? "true" : "false") << ",\"input_bytes\":" << input.size()
              << ",\"input_frame_ready\":" << (match.second ? "true" : "false") << ",\"next_send\":" << state.next_send
              << ",\"next_receive\":" << state.next_receive << ",\"queued_writes\":" << queued.size()
              << ",\"writing\":" << (writing ? "true" : "false") << ",\"read_completions\":" << read_completions
              << ",\"write_completions\":" << write_completions << ",\"last_write_kind\":" << last_write_kind
              << ",\"last_write_sequence\":" << last_write_sequence
              << ",\"pending_age_ms\":" << age(pending_since.load()) << ",\"last_read_age_ms\":" << age(last_read_at)
              << ",\"last_write_age_ms\":" << age(last_write_at) << ",\"kernel_readable_bytes\":" << available
              << ",\"available_error\":" << available_error.value()
              << ",\"receive_buffer_bytes\":" << (receive_error ? -1 : receive_buffer.value())
              << ",\"send_buffer_bytes\":" << (send_error ? -1 : send_buffer.value());
#if defined(__APPLE__) && defined(TCP_CONNECTION_INFO)
    tcp_connection_info info{};
    socklen_t length = sizeof(info);
    if (socket.is_open() && getsockopt(socket.native_handle(), IPPROTO_TCP, TCP_CONNECTION_INFO, &info, &length) == 0)
    {
        std::cout << ",\"tcp_info\":{\"state\":" << unsigned(info.tcpi_state)
                  << ",\"send_buffer_bytes\":" << info.tcpi_snd_sbbytes
                  << ",\"send_window_bytes\":" << info.tcpi_snd_wnd << ",\"receive_window_bytes\":" << info.tcpi_rcv_wnd
                  << ",\"rto_ms\":" << info.tcpi_rto << ",\"tx_bytes\":" << info.tcpi_txbytes
                  << ",\"rx_bytes\":" << info.tcpi_rxbytes << ",\"retransmit_bytes\":" << info.tcpi_txretransmitbytes
                  << ",\"out_of_order_bytes\":" << info.tcpi_rxoutoforderbytes << '}';
    }
    else
    {
        std::cout << ",\"tcp_info\":null,\"tcp_info_error\":" << (socket.is_open() ? errno : -1);
    }
#else
    std::cout << ",\"tcp_info\":null";
#endif
    std::cout << "}\n";
}

void session::receive(std::string_view bytes) {
    auto& a = app;
    connection_load::frame_header h;
    const bool server = a.o.role == "server";
    if (!connection_load::decode(bytes, h) || !state.accept(h, a.o, server))
    {
        ++a.state_errors;
        a.fail("protocol", "identity, phase, sequence or body mismatch");
        return;
    }
    if (h.kind == frame_kind::hello || h.kind == frame_kind::hello_ack)
    {
        if (server && !a.identities.emplace(h.owner.client, h.owner.connection).second)
        {
            ++a.state_errors;
            a.fail("protocol", "duplicate client/connection identity");
            return;
        }
        ++a.ready;
        pending_since = clock_type::time_point{};
        if (server)
        {
            send(connection_load::make_frame(frame_kind::hello_ack, state.owner));
        }
    }
    else if (h.kind == frame_kind::start || h.kind == frame_kind::start_ack)
    {
        ++a.started;
        pending_since = clock_type::time_point{};
        if (server)
        {
            send(connection_load::make_frame(frame_kind::start_ack, state.owner));
        }
    }
    else if (h.kind == frame_kind::data)
    {
        ++a.data_received;
        if (server)
        {
            send(std::string(bytes));
        }
        else
        {
            --a.outstanding;
            a.data_bytes -= header_bytes + a.o.payload;
            pending_since = state.next_receive < state.next_send ? sent_at[(state.next_receive - 1) % a.o.window]
                                                                 : clock_type::time_point{};
            fill();
            finish();
        }
    }
    else if (server)
    {
        send(connection_load::make_frame(frame_kind::finish_ack, state.owner, h.sequence));
    }
    else
    {
        ++a.finished;
        pending_since = clock_type::time_point{};
    }
}

void session::fill() {
    auto& a = app;
    while (a.traffic && !a.draining && !a.stopping && !a.failed && a.o.scenario == "traffic" &&
           state.next_send - state.next_receive < a.o.window)
    {
        if (a.outstanding >= a.o.inflight || header_bytes + a.o.payload > a.o.inflight_bytes - a.data_bytes)
        {
            ++a.rejected;
            a.fail("data admission", "global inflight count/byte limit exceeded");
            return;
        }
        if (state.next_send == (std::numeric_limits<std::uint64_t>::max)())
        {
            ++a.state_errors;
            a.fail("protocol", "sequence exhausted");
            return;
        }
        const auto sequence = state.next_send++;
        sent_at[(sequence - 1) % a.o.window] = clock_type::now();
        if (sequence == state.next_receive)
        {
            pending_since = clock_type::now();
        }
        ++a.outstanding;
        a.data_bytes += header_bytes + a.o.payload;
        a.peak_inflight = (std::max)(a.peak_inflight.load(), a.outstanding.load());
        send(connection_load::make_frame(frame_kind::data, state.owner, sequence, a.o.payload));
    }
}

void session::finish() {
    if (app.draining && !app.stopping && !app.failed && !state.finish_sent && state.next_receive == state.next_send)
    {
        state.finish_sent = true;
        pending_since = clock_type::now();
        send(connection_load::make_frame(frame_kind::finish, state.owner, state.next_send - 1));
    }
}

void application::accept() {
    if (stopping || failed)
    {
        return;
    }
    auto value = std::make_shared<session>(*this, io);
    ++callbacks;
    acceptor.async_accept(value->socket, asio::bind_executor(value->strand, [this, value](error_code error) {
        --callbacks;
        if (!stopping && error)
        {
            ++connect_errors;
            fail("accept callback", error.message());
        }
        else if (!stopping)
        {
            ++accepted;
            if (sessions.size() >= o.maximum || accepted > o.expected)
            {
                ++rejected;
                value->close();
                fail("accept admission", "expected/max connection count exceeded");
            }
            else
            {
                sessions.push_back(value);
                value->enable_trace();
                value->active = true;
                ++active;
                peak_active = (std::max)(peak_active.load(), active.load());
                error_code option_error;
                value->socket.set_option(tcp::no_delay(true), option_error);
                if (option_error)
                {
                    fail("TCP_NODELAY", option_error.message());
                }
                else
                {
                    value->read();
                    accept();
                }
            }
        }
        drained();
    }));
}

void application::launch() {
    if (clients.size() >= o.maximum || connecting >= o.parallel)
    {
        ++rejected;
        fail("connect admission", "active/connecting limit exceeded");
        return;
    }
    auto value = std::make_shared<session>(*this, io);
    clients.push_back(value);
    value->state.owner = {o.run, o.client, ++attempted};
    value->sent_at.resize(o.window);
    value->connecting = true;
    value->connect_since = clock_type::now();
    ++connecting;
    peak_connecting = (std::max)(peak_connecting.load(), connecting.load());
    asio::post(value->strand, [this, value] {
        if (stopping || failed)
        {
            --connecting;
            value->connecting = false;
            value->close();
            drained();
            return;
        }
        sessions.push_back(value);
        value->enable_trace();
        ++callbacks;
        value->socket.async_connect(tcp::endpoint(asio::ip::make_address(o.host), static_cast<unsigned short>(o.port)),
            asio::bind_executor(value->strand, [this, value](error_code error) {
                --callbacks;
                --connecting;
                value->connecting = false;
                if (!stopping && error)
                {
                    ++connect_errors;
                    fail("connect callback", error.message());
                }
                else if (!stopping)
                {
                    ++established;
                    ++active;
                    peak_active = (std::max)(peak_active.load(), active.load());
                    value->active = true;
                    error_code option_error;
                    value->socket.set_option(tcp::no_delay(true), option_error);
                    if (option_error)
                    {
                        fail("TCP_NODELAY", option_error.message());
                    }
                    else
                    {
                        value->pending_since = clock_type::now();
                        value->read();
                        value->send(connection_load::make_frame(
                            frame_kind::hello, value->state.owner, o.scenario == "traffic" ? 1 : 0));
                    }
                }
                drained();
            }));
    });
}

void application::poll() {
    if (stopping || failed)
    {
        return;
    }
    const auto now = clock_type::now();
    if (o.role == "client")
    {
        if (attempted < o.connections && connecting < o.parallel && now >= next_launch)
        {
            launch();
            next_launch = now + std::chrono::nanoseconds(1000000000 / o.rate);
        }
        if (!starting && ready == o.connections)
        {
            starting = true;
            for (const auto& value : clients)
            {
                asio::post(value->strand, [value] {
                    value->pending_since = clock_type::now();
                    value->send(connection_load::make_frame(frame_kind::start, value->state.owner));
                });
            }
        }
        if (!traffic && started == o.connections)
        {
            traffic = true;
            until = now + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(o.hold));
            for (const auto& value : clients)
            {
                asio::post(value->strand, [value] { value->fill(); });
            }
        }
        if (traffic && !draining && now >= until)
        {
            draining = true;
            for (const auto& value : clients)
            {
                asio::post(value->strand, [value] { value->finish(); });
            }
        }
        if (finished == o.connections && !outstanding && !pending_writes)
        {
            stop();
            return;
        }
        if (now < next_timeout_scan)
        {
            return;
        }
        const auto timeout = std::chrono::milliseconds((std::min)(o.connect_ms, o.request_ms));
        next_timeout_scan = now + std::clamp(timeout / 4, std::chrono::milliseconds(1), std::chrono::milliseconds(50));
        for (const auto& value : clients)
        {
            if (value->connecting && now - value->connect_since.load() >= std::chrono::milliseconds(o.connect_ms))
            {
                ++timeouts;
                fail("connect timeout", "pending connect exceeded connect-timeout-ms");
                return;
            }
            const auto pending = value->pending_since.load();
            if (pending != clock_type::time_point{} && now - pending >= std::chrono::milliseconds(o.request_ms))
            {
                ++timeouts;
                fail("request timeout", "pending response exceeded request-timeout-ms");
                return;
            }
        }
    }
    else if (finished == o.expected && disconnected == o.expected && !pending_writes)
    {
        stop();
        return;
    }
}

void application::sample_kernel() {
    // Reuse the existing marker on the single IO worker. Release recovered slots
    // before admission so persistent stalls are never evicted by transient ones.
    if (stopping || failed || o.scenario != "traffic")
    {
        return;
    }
    const auto now = clock_type::now();
    const auto idle = [now](const session& value) {
        return std::chrono::duration_cast<std::chrono::microseconds>(
            now - (std::max)(value.last_read_at, value.last_write_at))
            .count();
    };
    const auto stalled = [&](const session& value) {
        return connection_load::tcp_kernel_samples::stalled(
            value.active, value.state.finished, value.closed, value.state.next_receive > 1, idle(value));
    };
    for (const auto& value : sessions)
    {
        kernel_samples.prune(value->state.owner, stalled(*value), value->read_completions + value->write_completions);
    }
    for (const auto& value : sessions)
    {
        if (stalled(*value))
        {
            kernel_samples.sample(value->state.owner, elapsed_us(), idle(*value),
                value->read_completions + value->write_completions, value->state.next_receive, value->state.next_send,
                [&] {
#if defined(__APPLE__)
                    return connection_load::query_tcp_kernel_info(
                        value->socket.is_open() ? value->socket.native_handle() : -1);
#else
                    return connection_load::query_tcp_kernel_info(-1);
#endif
                });
        }
    }
}

void application::sample() {
    const auto now = clock_type::now();
    if (now - sampled < std::chrono::seconds(1))
    {
        return;
    }
    if (operations.request_marker())
    {
        const auto submitted = elapsed_us();
        asio::post(io, [this, submitted] {
            operations.complete_marker(submitted, elapsed_us());
            if (o.tcp_kernel_samples)
            {
                sample_kernel();
            }
        });
    }
    // Bracket the CPU query with monotonic phase counts so consecutive sample
    // endpoints exclude overlapping START and FIN processing from idle intervals.
    const auto started_before_resources = started.load();
    const auto metrics = resources.sample();
    const auto finished_after_resources = finished.load();
    std::cout << "{\"event\":\"sample\",\"role\":" << std::quoted(o.role)
              << ",\"elapsed_s\":" << std::chrono::duration<double>(now - began).count()
              << ",\"sample_interval_ms\":1000,\"active\":" << active << ",\"ready\":" << ready
              << ",\"started\":" << started_before_resources << ",\"finished\":" << finished_after_resources
              << ",\"pending_send_bytes\":" << pending_bytes;
    connection_load::write_resource_fields(std::cout, metrics, false);
    std::cout << "}\n" << std::flush;
    sampled = now;
}

void application::run() {
    connection_load::write_resource_startup(std::cout, resources.initial_snapshot(), o.role, o.run, o.client);
    if (o.role == "server")
    {
        const tcp::endpoint endpoint(asio::ip::make_address(o.host), static_cast<unsigned short>(o.port));
        acceptor.open(endpoint.protocol());
        acceptor.bind(endpoint);
        acceptor.listen(asio::socket_base::max_listen_connections);
        asio::post(io, [this] { accept(); });
        std::cout << "{\"event\":\"ready\",\"port\":" << acceptor.local_endpoint().port() << "}\n" << std::flush;
    }
    signals.async_wait([this](error_code error, int) {
        if (!error)
        {
            fail("signal", "interrupted");
        }
    });
    const auto until_deadline =
        began + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(o.deadline));
    std::thread worker([this] { io.run(); });
    while (!stopping)
    {
        if (failed)
        {
            stop();
        }
        else if (clock_type::now() >= until_deadline)
        {
            ++timeouts;
            fail("deadline", "target incomplete");
            stop();
        }
        else
        {
            poll();
        }
        if (!stopping)
        {
            sample();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    worker.join();
}

void application::result() const {
    const auto expected = o.role == "client" ? o.connections : o.expected;
    const bool clean = stopping && !cleanup_timeout && !callbacks && !pending_writes && !pending_bytes &&
                       !pending_data_writes && !active && !connecting;
    const bool complete = ready == expected && started == expected && finished == expected &&
                          data_sent == data_received && !outstanding && (o.scenario == "idle" || data_received > 0);
    std::cout << "{\"event\":\"result\",\"tool\":\"arknet_asio_connection_control\",\"dependency\":"
              << std::quoted(dependency) << ",\"dependency_version\":" << dependency_version
              << ",\"scenario\":" << std::quoted(o.scenario) << ",\"run_id\":" << o.run << ",\"client_id\":" << o.client
              << ",\"target_connections\":" << expected << ",\"payload_bytes\":" << o.payload
              << ",\"window\":" << o.window << ",\"connect_rate_limit\":" << o.rate
              << ",\"connect_parallel_limit\":" << o.parallel
              << ",\"io_contexts\":1,\"io_workers\":1,\"controller_on_io_worker\":false,\"initial_buffer_bytes\":4096,"
                 "\"tcp_nodelay\":true,\"max_"
                 "buffer_bytes\":4096"
              << ",\"role\":" << std::quoted(o.role)
              << ",\"success\":" << (failure.empty() && clean && complete ? "true" : "false")
              << ",\"cleanup_complete\":" << (clean ? "true" : "false") << ",\"error\":" << std::quoted(failure)
              << ",\"attempted\":" << attempted << ",\"accepted\":" << accepted << ",\"established\":" << established
              << ",\"ready\":" << ready << ",\"started\":" << started << ",\"finished\":" << finished
              << ",\"active\":" << active << ",\"connecting\":" << connecting << ",\"disconnected\":" << disconnected
              << ",\"data_sent\":" << data_sent << ",\"data_received\":" << data_received
              << ",\"outstanding\":" << outstanding << ",\"pending_writes\":" << pending_writes
              << ",\"pending_bytes\":" << pending_bytes << ",\"socket_callbacks\":" << callbacks
              << ",\"write_errors\":" << write_errors << ",\"state_errors\":" << state_errors
              << ",\"connect_errors\":" << connect_errors << ",\"timeouts\":" << timeouts
              << ",\"rejected\":" << rejected << ",\"cancelled_writes\":" << cancelled_writes
              << ",\"peak_active\":" << peak_active << ",\"peak_connecting\":" << peak_connecting
              << ",\"peak_data_inflight\":" << peak_inflight << ",\"peak_pending_bytes\":" << peak_bytes
              << ",\"max_connections\":" << o.maximum << ",\"max_inflight\":" << o.inflight
              << ",\"max_inflight_bytes\":" << o.inflight_bytes
              << ",\"elapsed_seconds\":" << std::chrono::duration<double>(clock_type::now() - began).count()
              << ",\"cleanup_seconds\":"
              << (drained_at == clock_type::time_point{}
                         ? -1
                         : std::chrono::duration<double>(drained_at - stopped_at).count());
    if (operations.enabled())
    {
        std::cout << ",\"operation_trace_enabled\":true,\"operation_trace_emitted\":"
                  << (trace_emitted ? "true" : "false");
        if (o.socket_operation_trace)
        {
            std::cout << ",\"socket_operation_trace_enabled\":true";
        }
        if (o.tcp_kernel_samples)
        {
            std::cout << ",\"tcp_kernel_samples_enabled\":true";
        }
    }
    std::cout << "}\n";
    if (!failure.empty() || !clean || !complete)
    {
        throw std::runtime_error(failure.empty() ? "terminal state incomplete" : failure);
    }
}

void self_test() {
    auto require = [](bool ok) {
        if (!ok)
        {
            throw std::runtime_error("native control self-test failed");
        }
    };
    auto parse = [](const std::string& mode, bool socket_trace = false, bool kernel_samples = false) {
        std::vector<std::string> values{"control", "--role", "client", "--target", "127.0.0.1", "--port", "1",
            "--run-id", "1", "--operation-trace", mode};
        if (socket_trace)
        {
            values.insert(values.end(), {"--socket-operation-trace", "on"});
        }
        if (kernel_samples)
        {
            values.insert(values.end(), {"--tcp-kernel-samples", "on"});
        }
        std::vector<char*> argv;
        for (auto& value : values)
        {
            argv.push_back(value.data());
        }
        return arguments(static_cast<int>(argv.size()), argv.data());
    };
    const auto disabled = parse("off"), enabled = parse("on");
    const auto socket_enabled = parse("on", true);
    const auto kernel_enabled = parse("on", false, true);
    require(!disabled.tcp_kernel_samples && !enabled.tcp_kernel_samples && kernel_enabled.tcp_kernel_samples);
    require(kernel_enabled.deadline == enabled.deadline && kernel_enabled.request_ms == enabled.request_ms &&
            kernel_enabled.connections == enabled.connections && kernel_enabled.window == enabled.window &&
            kernel_enabled.payload == enabled.payload);
    bool kernel_rejected{};
    try
    { parse("off", false, true); }
    catch (const std::invalid_argument&)
    { kernel_rejected = true; }
    require(kernel_rejected);
    require(!disabled.operation_trace && enabled.operation_trace);
    require(
        !disabled.socket_operation_trace && !enabled.socket_operation_trace && socket_enabled.socket_operation_trace);
    require(socket_enabled.deadline == enabled.deadline && socket_enabled.request_ms == enabled.request_ms &&
            socket_enabled.connections == enabled.connections && socket_enabled.window == enabled.window &&
            socket_enabled.payload == enabled.payload);
    bool socket_rejected{};
    try
    { parse("off", true); }
    catch (const std::invalid_argument&)
    { socket_rejected = true; }
    require(socket_rejected);
    require(disabled.deadline == enabled.deadline && disabled.request_ms == enabled.request_ms &&
            disabled.connections == enabled.connections && disabled.window == enabled.window &&
            disabled.payload == enabled.payload);
    options o;
    o.role = "client";
    o.host = "127.0.0.1";
    o.port = 1;
    o.run = 7;
    o.scenario = "traffic";
    o.connections = 5000;
    o.expected = 5000;
    o.window = 4;
    validate(o);
    auto rejected = [&](options value) {
        try
        { validate(value); }
        catch (const std::invalid_argument&)
        { return true; }
        return false;
    };
    auto invalid = o;
    invalid.inflight = 19999;
    require(rejected(invalid));
    invalid = o;
    invalid.inflight_bytes = 20000 * (header_bytes + o.payload) - 1;
    require(rejected(invalid));
    invalid = o;
    invalid.budget = 4096;
    require(rejected(invalid));
    invalid = o;
    invalid.maximum = 4999;
    require(rejected(invalid));
    invalid = o;
    invalid.parallel = 0;
    require(rejected(invalid));
    invalid = o;
    invalid.payload = 4024;
    require(rejected(invalid));
    for (const auto size : {std::size_t{64}, std::size_t{253}, std::size_t{254}, std::size_t{1088}, std::size_t{4093}})
    {
        const wire_frame value(std::string(size, 'x'));
        const std::string wire(value.prefix.data(), value.prefix_size);
        const auto bytes = wire + value.body + "tail";
        for (std::size_t length = 0; length < wire.size() + size; ++length)
        {
            require(!dgram_match{}(bytes.begin(), bytes.begin() + length).second);
        }
        const auto match = dgram_match{}(bytes.begin(), bytes.end());
        require(match.second && std::size_t(match.first - bytes.begin()) == wire.size() + size);
    }
    for (const std::string bad : {std::string("\xfe\x01\x00", 3), std::string("\xff\x01\0\0\0\0\0\0\0", 9),
             std::string("\xfe\x00\x10", 3), std::string("\xff\0\0\x01\0\0\0\0\0", 9)})
    {
        const auto match = dgram_match{}(bad.begin(), bad.end());
        require(match.second && match.first == bad.begin());
    }
    auto feed = [&](protocol_state& state, bool server, frame_kind kind, identity owner, std::uint64_t sequence = 0) {
        connection_load::frame_header h;
        require(connection_load::decode(
            connection_load::make_frame(kind, owner, sequence, kind == frame_kind::data ? o.payload : 0), h));
        return state.accept(h, o, server);
    };
    const identity owner{7, 9, 1}, wrong{7, 9, 2};
    protocol_state server, client;
    client.owner = owner;
    require(!feed(server, true, frame_kind::start, owner));
    require(feed(server, true, frame_kind::hello, owner, 1));
    require(!feed(server, true, frame_kind::hello, owner, 1));
    require(feed(client, false, frame_kind::hello_ack, owner));
    require(feed(server, true, frame_kind::start, owner));
    require(feed(client, false, frame_kind::start_ack, owner));
    require(!feed(server, true, frame_kind::data, wrong, 1));
    require(!feed(server, true, frame_kind::data, owner, 2));
    require(feed(server, true, frame_kind::data, owner, 1));
    require(!feed(client, false, frame_kind::data, owner, 1));
    client.next_send = 2;
    require(feed(client, false, frame_kind::data, owner, 1));
    require(!feed(client, false, frame_kind::data, owner, 1));
    require(!feed(server, true, frame_kind::finish, owner, 0));
    require(feed(server, true, frame_kind::finish, owner, 1));
    require(!feed(client, false, frame_kind::finish_ack, owner, 1));
    client.finish_sent = true;
    require(feed(client, false, frame_kind::finish_ack, owner, 1));
    require(!feed(server, true, frame_kind::data, owner, 2));
    // Composed-only diagnostics stay in the original completion body. The
    // optional socket adaptor has separate focused association tests.
    asio::io_context context;
    auto strand = asio::make_strand(context);
    asio::cancellation_signal cancellation;
    std::pmr::unsynchronized_pool_resource memory;
    std::pmr::polymorphic_allocator<std::byte> allocator(&memory);
    connection_load::operation_ring recorded;
    bool called = false;
    auto handler = asio::bind_executor(strand,
        asio::bind_allocator(allocator, asio::bind_cancellation_slot(cancellation.slot(), [&](error_code error) {
            require(strand.running_in_this_thread());
            require(error == asio::error::operation_aborted);
            require(!called);
            called = true;
            recorded.record(connection_load::operation_phase::read_complete, 2, 0, 1, error.value());
        })));
    require(asio::get_associated_executor(handler) == strand);
    require(asio::get_associated_allocator(handler).resource() == &memory);
    require(asio::get_associated_cancellation_slot(handler) == cancellation.slot());
    asio::steady_timer timer(context, std::chrono::hours(1));
    recorded.record(connection_load::operation_phase::read_start, 1, 0, 1);
    timer.async_wait(std::move(handler));
    cancellation.emit(asio::cancellation_type::all);
    context.run_for(std::chrono::seconds(1));
    require(called && recorded.size() == 2 && recorded.at(1).error != 0);
    require(recorded.at(0).operation_id == recorded.at(1).operation_id);
    std::cout << "native control bound executor, allocator and cancellation checks passed\n";
    std::cout << "native control configuration, dgram, identity and sequence checks passed\n";
}
} // namespace

int main(int argc, char** argv) {
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            help();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--self-test")
        {
            self_test();
            return 0;
        }
        application app(arguments(argc, argv));
        // A blocked callback/stdout cannot turn the asynchronous deadline into
        // an unlimited hang. This watchdog emits no synthetic final result.
        std::mutex mutex;
        std::condition_variable wake;
        bool done = false;
        std::thread watchdog([&] {
            std::unique_lock lock(mutex);
            if (!wake.wait_until(lock,
                    app.began + std::chrono::duration_cast<clock_type::duration>(
                                    std::chrono::duration<double>(app.o.deadline + app.o.cleanup)),
                    [&] { return done; }))
            {
                std::_Exit(124);
            }
        });
        try
        {
            app.run();
            app.result();
        }
        catch (...)
        {
            {
                std::lock_guard lock(mutex);
                done = true;
            }
            wake.notify_one();
            watchdog.join();
            throw;
        }
        {
            std::lock_guard lock(mutex);
            done = true;
        }
        wake.notify_one();
        watchdog.join();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "native control: " << error.what() << '\n';
        return 1;
    }
}
