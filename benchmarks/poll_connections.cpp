// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
//
// Diagnostic TCP peer without Asio or strands. One thread drives non-blocking
// sockets with poll(2), or with --events kqueue a loop that registers events the
// way Boost.Asio's kqueue reactor does. It speaks the native Asio control's wire
// protocol, so either role can be paired with arknet_asio_connection_control.

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <linux/sockios.h>

#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <sys/event.h>

#define ARKNET_POLL_CONTROL_HAS_KQUEUE 1
#else
#define ARKNET_POLL_CONTROL_HAS_KQUEUE 0
#endif

#include "connections_io_histogram.hpp"
#include "connections_kernel_samples.hpp"
#include "connections_protocol.hpp"

namespace {

using clock_type = std::chrono::steady_clock;
using connection_load::frame_kind;
using connection_load::header_bytes;

constexpr std::size_t max_wire_bytes = 4096;

volatile std::sig_atomic_t signalled = 0;

struct options
{
    std::string role, host = "127.0.0.1";
    int port = -1;
    std::uint64_t run = 1, client = 1, connections = 0, expected = 0, rate = 1000, parallel = 32, payload = 1024,
                  window = 4, connect_ms = 5000, request_ms = 5000;
    double hold = 3, deadline = 40, observe = 0;
    bool kqueue_events{}, serial_writes{}, serial_reads{}, asio_socket_setup{}, send_queue_watch{};
};

std::uint64_t integer(const std::string& value) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
    {
        throw std::invalid_argument("expected a decimal integer: " + value);
    }
    return std::stoull(value);
}

double seconds(const std::string& value) {
    std::size_t used{};
    const auto result = std::stod(value, &used);
    if (used != value.size() || !(result >= 0))
    {
        throw std::invalid_argument("expected non-negative seconds: " + value);
    }
    return result;
}

options parse(const std::vector<std::string>& arguments) {
    options o;
    std::set<std::string> seen;
    if (arguments.size() % 2)
    {
        throw std::invalid_argument("options must be --key value pairs");
    }
    for (std::size_t index = 0; index < arguments.size(); index += 2)
    {
        const auto& key = arguments[index];
        const auto& value = arguments[index + 1];
        if (!seen.insert(key).second)
        {
            throw std::invalid_argument("duplicate option " + key);
        }
        if (key == "--role")
        {
            o.role = value;
        }
        else if (key == "--listen" || key == "--target")
        {
            o.host = value;
        }
        else if (key == "--port")
        {
            const auto port = integer(value);
            if (port > 65535)
            {
                throw std::invalid_argument("port out of range");
            }
            o.port = static_cast<int>(port);
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
        else if (key == "--hold-seconds")
        {
            o.hold = seconds(value);
        }
        else if (key == "--deadline-seconds")
        {
            o.deadline = seconds(value);
        }
        else if (key == "--observe-seconds")
        {
            o.observe = seconds(value);
        }
        else if (key == "--send-queue-watch" && (value == "off" || value == "on"))
        {
            o.send_queue_watch = value == "on";
        }
        else if (key == "--socket-setup" && (value == "default" || value == "asio"))
        {
            o.asio_socket_setup = value == "asio";
        }
        else if (key == "--read-pacing" && (value == "drain" || value == "serial"))
        {
            o.serial_reads = value == "serial";
        }
        else if (key == "--write-pacing" && (value == "burst" || value == "serial"))
        {
            o.serial_writes = value == "serial";
        }
        else if (key == "--events" && (value == "poll" || value == "kqueue"))
        {
            if (value == "kqueue" && !ARKNET_POLL_CONTROL_HAS_KQUEUE)
            {
                throw std::invalid_argument("--events kqueue is unavailable on this platform");
            }
            o.kqueue_events = value == "kqueue";
        }
        else
        {
            throw std::invalid_argument("unknown option " + key);
        }
    }
    const bool server = o.role == "server";
    const bool client = o.role == "client";
    in_addr address{};
    if ((!server && !client) || o.port < 0 || (client && o.port == 0) ||
        (server && (seen.contains("--target") || seen.contains("--connections") || seen.contains("--client-id") ||
                       seen.contains("--observe-seconds"))) ||
        (client && (seen.contains("--listen") || seen.contains("--expected-connections"))) ||
        inet_pton(AF_INET, o.host.c_str(), &address) != 1)
    {
        throw std::invalid_argument("missing/invalid role, IPv4 address or port, or role-inappropriate option");
    }
    if (client)
    {
        o.expected = o.connections;
    }
    if (!o.run || !o.client || !o.expected || !o.rate || !o.parallel || !o.window || !o.connect_ms || !o.request_ms ||
        !o.payload || o.payload > 4023 || !(o.hold > 0) || !(o.deadline > o.hold))
    {
        throw std::invalid_argument("invalid count, payload, timeout, hold or deadline");
    }
    return o;
}

void append_frame(std::string& output, const std::string& body) {
    const auto size = body.size();
    if (size < 254)
    {
        output.push_back(static_cast<char>(size));
    }
    else
    {
        output.push_back(static_cast<char>(254));
        output.push_back(static_cast<char>(size & 0xff));
        output.push_back(static_cast<char>(size >> 8));
    }
    output += body;
}

enum class frame_status
{
    incomplete,
    invalid,
    complete
};

struct frame_view
{
    frame_status status = frame_status::incomplete;
    std::size_t prefix{}, size{};
};

// Same canonical-length rules and 4096-byte wire cap as the native control.
frame_view inspect(std::string_view bytes) {
    if (bytes.empty())
    {
        return {};
    }
    const auto marker = static_cast<unsigned char>(bytes[0]);
    const std::size_t width = marker == 255 ? 8 : marker == 254 ? 2 : 0;
    if (bytes.size() < 1 + width)
    {
        return {};
    }
    std::uint64_t size = width ? 0 : marker;
    for (std::size_t byte = 0; byte < width; ++byte)
    {
        size |= std::uint64_t(static_cast<unsigned char>(bytes[1 + byte])) << (byte * 8);
    }
    if ((width == 2 && size < 254) || (width == 8 && size < 65536) || size > max_wire_bytes - 1 - width)
    {
        return {frame_status::invalid};
    }
    if (bytes.size() - 1 - width < size)
    {
        return {frame_status::incomplete, 1 + width, static_cast<std::size_t>(size)};
    }
    return {frame_status::complete, 1 + width, static_cast<std::size_t>(size)};
}

std::uint64_t elapsed_ms(clock_type::time_point from, clock_type::time_point to) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count());
}

struct session
{
    int fd = -1;
    connection_load::identity owner{};
    bool connecting{}, hello{}, started{}, finish_sent{}, finished{}, peer_eof{};
    std::uint64_t next_send = 1, next_receive = 1;
    std::vector<clock_type::time_point> sent_at;
    clock_type::time_point pending_since{}, connect_since{}, last_read_at{};
    std::string output, input;
    std::size_t output_offset{};
    std::uint64_t bytes_written{}, bytes_read{};
    bool watched{};
    int kevents{}; // kqueue filters registered: 0, READ, or READ and WRITE.
    // --write-pacing serial: frames waiting behind the one being written, and
    // whether the written frame's completion still awaits the next loop pass.
    std::deque<std::string> queued;
    bool writing{}, completion_posted{};
    // --read-pacing serial: waiting for readiness, or a read completion awaits the next pass.
    bool read_waiting{}, read_posted{};
    // --send-queue-watch: last kernel send-queue size, when it last changed or
    // emptied, and how many stall events this session has emitted.
    long long send_queue_last{};
    clock_type::time_point send_queue_since{};
    int send_queue_reports{};
};

class peer
{
public:
    peer(options value, std::ostream& out, std::atomic<int>* published_port = nullptr)
        : o_(std::move(value))
        , out_(out)
        , published_port_(published_port) {}

    peer(const peer&) = delete;
    peer& operator=(const peer&) = delete;

    ~peer() {
        close_all();
        if (listener_ >= 0)
        {
            ::close(listener_);
        }
        if (kqueue_ >= 0)
        {
            ::close(kqueue_);
        }
    }

    int run();

private:
    bool client() const {
        return o_.role == "client";
    }

    void open_listener();
    void launch();
    void connected(session& s);
    void connect_ready(session& s);
    void wait_poll();
#if ARKNET_POLL_CONTROL_HAS_KQUEUE
    void register_events(std::uintptr_t id, int fd, int count, int& registered);
    void wait_kqueue();
#endif
    void accept_all();
    void readable(session& s);
    void flush(session& s);
    void start_write(session& s);
    void run_posted();
    void read_step(session& s);
    void read_resume(session& s);
    void post(session& s, bool read);
    void read_ready(session& s);
    static bool output_idle(const session& s);
    void send(session& s, const std::string& body);
    void frame(session& s, std::string_view body);
    void client_frame(session& s, const connection_load::frame_header& h);
    void server_frame(session& s, const connection_load::frame_header& h);
    void fill(session& s);
    void finish(session& s);
    void eof(session& s);
    void close(session& s);
    void close_all();
    void fail(const std::string& scope, const std::string& reason);
    void advance(clock_type::time_point now);
    void snapshot(const char* phase, clock_type::time_point now);
    void watch_send_queues(clock_type::time_point now);
    void emit_send_queue(const session& s, const char* phase, clock_type::time_point now, long long queued);
    void result(clock_type::time_point now) const;
    bool complete() const;

    options o_;
    std::ostream& out_;
    std::atomic<int>* published_port_;
    int listener_ = -1, kqueue_ = -1;
    bool listener_registered_{};
    std::vector<pollfd> descriptors_;
    // Posted completions: session index and whether it is a read (else a write).
    std::vector<std::pair<std::size_t, bool>> posted_, posted_ready_;
    std::vector<std::size_t> owners_;
    std::vector<session> sessions_;
    std::set<std::pair<std::uint64_t, std::uint64_t>> identities_;
    connection_load::io_histogram histogram_;
    clock_type::time_point began_ = clock_type::now(), next_launch_ = began_, until_{}, failed_at_{}, latest_read_{},
                           next_watch_ = began_;
    std::size_t send_queue_tracked_{};
    std::uint64_t attempted_{}, connecting_{}, established_{}, accepted_{}, ready_{}, started_{}, finished_{},
        disconnected_{}, outstanding_{}, data_sent_{}, data_received_{}, timeouts_{};
    bool starting_{}, traffic_{}, draining_{}, failed_{}, snapshot_taken_{};
    std::string failure_scope_, failure_reason_;
};

// --socket-setup asio follows Boost.Asio's socket_ops on Apple/FreeBSD: SO_NOSIGPIPE
// on every created and accepted socket, and non-blocking mode through
// ioctl(FIONBIO). The default sets O_NONBLOCK with fcntl and relies on SIGPIPE
// being ignored.
void set_nonblocking(int fd, bool asio_setup) {
    if (asio_setup)
    {
#if defined(SO_NOSIGPIPE)
        int enabled = 1;
        if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0)
        {
            throw std::runtime_error(std::string("SO_NOSIGPIPE: ") + std::strerror(errno));
        }
#endif
        int value = 1;
        if (::ioctl(fd, FIONBIO, &value) != 0)
        {
            throw std::runtime_error(std::string("FIONBIO: ") + std::strerror(errno));
        }
        return;
    }
    const auto flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0)
    {
        throw std::runtime_error(std::string("O_NONBLOCK: ") + std::strerror(errno));
    }
}

void set_no_delay(int fd) {
    int enabled = 1;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0)
    {
        throw std::runtime_error(std::string("TCP_NODELAY: ") + std::strerror(errno));
    }
}

int port_of(int fd, bool remote) {
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    const auto result = remote ? ::getpeername(fd, reinterpret_cast<sockaddr*>(&address), &length)
                               : ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length);
    return result == 0 ? ntohs(address.sin_port) : -1;
}

long long readable_bytes(int fd) {
    int value{};
    return ::ioctl(fd, FIONREAD, &value) == 0 ? value : -1;
}

// Bytes the kernel still holds in the send queue (unsent or unacknowledged).
long long send_queue_bytes(int fd) {
#if defined(SO_NWRITE)
    int value{};
    socklen_t length = sizeof(value);
    return ::getsockopt(fd, SOL_SOCKET, SO_NWRITE, &value, &length) == 0 ? value : -1;
#elif defined(SIOCOUTQ)
    int value{};
    return ::ioctl(fd, SIOCOUTQ, &value) == 0 ? value : -1;
#else
    (void)fd;
    return -1;
#endif
}

// Apple TCP_CONNECTION_INFO through the shared kernel-sample decoder; elsewhere
// the status is "unsupported" and no counters are written.
void write_tcp_info(std::ostream& out, int fd) {
    const auto info = connection_load::query_tcp_kernel_info(fd);
    using status = connection_load::tcp_kernel_info::status;
    if (info.result != status::available)
    {
        out << "{\"status\":\""
            << (info.result == status::unsupported         ? "unsupported"
                   : info.result == status::closed         ? "closed"
                   : info.result == status::short_response ? "short_response"
                                                           : "query_failed")
            << "\",\"error\":" << info.error << '}';
        return;
    }
    out << "{\"status\":\"available\",\"state\":" << info.state << ",\"flags\":" << info.flags
        << ",\"rto_ms\":" << info.rto_ms << ",\"send_buffer_bytes\":" << info.send_buffer_bytes
        << ",\"send_window_bytes\":" << info.send_window_bytes
        << ",\"receive_window_bytes\":" << info.receive_window_bytes
        << ",\"congestion_window_bytes\":" << info.congestion_window_bytes
        << ",\"slow_start_threshold_bytes\":" << info.slow_start_threshold_bytes << ",\"tx_bytes\":" << info.tx_bytes
        << ",\"rx_bytes\":" << info.rx_bytes << ",\"retransmit_bytes\":" << info.retransmit_bytes
        << ",\"out_of_order_bytes\":" << info.out_of_order_bytes << '}';
}

void peer::open_listener() {
    listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0)
    {
        throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
    }
    int enabled = 1;
    ::setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(o_.port));
    inet_pton(AF_INET, o_.host.c_str(), &address.sin_addr);
    if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener_, SOMAXCONN) != 0)
    {
        throw std::runtime_error(std::string("bind/listen: ") + std::strerror(errno));
    }
    set_nonblocking(listener_, o_.asio_socket_setup);
    const auto port = port_of(listener_, false);
    if (published_port_)
    {
        published_port_->store(port);
    }
    out_ << "{\"event\":\"ready\",\"tool\":\"poll_control\",\"port\":" << port << "}\n" << std::flush;
}

void peer::launch() {
    session s;
    s.owner = {o_.run, o_.client, ++attempted_};
    s.sent_at.resize(o_.window);
    s.connect_since = clock_type::now();
    s.fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s.fd < 0)
    {
        fail("socket", std::strerror(errno));
        return;
    }
    set_nonblocking(s.fd, o_.asio_socket_setup);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(o_.port));
    inet_pton(AF_INET, o_.host.c_str(), &address.sin_addr);
    const auto result = ::connect(s.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    if (result != 0 && errno != EINPROGRESS)
    {
        const auto reason = std::string(std::strerror(errno));
        ::close(s.fd);
        fail("connect", reason);
        return;
    }
    sessions_.push_back(std::move(s));
    auto& value = sessions_.back();
    if (result == 0)
    {
        connected(value);
    }
    else
    {
        value.connecting = true;
        ++connecting_;
    }
}

void peer::connected(session& s) {
    if (s.connecting)
    {
        s.connecting = false;
        --connecting_;
    }
    ++established_;
    set_no_delay(s.fd);
    s.pending_since = clock_type::now();
    // The native control starts its read before sending HELLO.
    if (o_.serial_reads)
    {
        read_step(s);
    }
    send(s, connection_load::make_frame(frame_kind::hello, s.owner, 1));
}

void peer::connect_ready(session& s) {
    int error{};
    socklen_t length = sizeof(error);
    ::getsockopt(s.fd, SOL_SOCKET, SO_ERROR, &error, &length);
    if (error)
    {
        fail("connect callback", std::strerror(error));
        close(s);
        return;
    }
    connected(s);
}

void peer::accept_all() {
    while (!failed_)
    {
        const auto fd = ::accept(listener_, nullptr, nullptr);
        if (fd < 0)
        {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ECONNABORTED && errno != EINTR)
            {
                fail("accept", std::strerror(errno));
            }
            return;
        }
        if (++accepted_ > o_.expected)
        {
            ::close(fd);
            fail("accept admission", "expected connection count exceeded");
            return;
        }
        set_nonblocking(fd, o_.asio_socket_setup);
        set_no_delay(fd);
        session s;
        s.fd = fd;
        s.last_read_at = clock_type::now();
        sessions_.push_back(std::move(s));
        if (o_.serial_reads)
        {
            read_step(sessions_.back());
        }
    }
}

void peer::flush(session& s) {
    while (s.fd >= 0 && s.output_offset < s.output.size())
    {
        const auto requested = s.output.size() - s.output_offset;
        const auto sent = ::send(s.fd, s.output.data() + s.output_offset, requested, 0);
        if (sent < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                ++histogram_.write.would_block;
                return;
            }
            fail("send", std::strerror(errno));
            return;
        }
        histogram_.write.record(requested, static_cast<std::size_t>(sent));
        s.output_offset += static_cast<std::size_t>(sent);
        s.bytes_written += static_cast<std::uint64_t>(sent);
    }
    s.output.clear();
    s.output_offset = 0;
    if (s.writing)
    {
        // Like an Asio write completion: posted, never run inline.
        s.writing = false;
        s.completion_posted = true;
        post(s, false);
        return;
    }
    if (s.peer_eof && s.fd >= 0)
    {
        close(s);
    }
}

// Serial pacing mirrors the native control's one async_write per session: a new
// frame is attempted immediately only when no write is outstanding; the next
// queued frame starts when the previous completion runs on a later loop pass.
void peer::start_write(session& s) {
    if (s.fd < 0 || s.writing || s.completion_posted || s.queued.empty())
    {
        return;
    }
    s.output = std::move(s.queued.front());
    s.queued.pop_front();
    s.output_offset = 0;
    s.writing = true;
    flush(s);
}

void peer::post(session& s, bool read) {
    posted_.emplace_back(static_cast<std::size_t>(&s - sessions_.data()), read);
}

void peer::run_posted() {
    posted_ready_.clear();
    posted_ready_.swap(posted_);
    for (const auto& [index, read] : posted_ready_)
    {
        auto& s = sessions_[index];
        if (read)
        {
            read_resume(s);
            continue;
        }
        s.completion_posted = false;
        if (s.fd < 0)
        {
            continue;
        }
        if (!s.queued.empty())
        {
            start_write(s);
        }
        else if (s.peer_eof)
        {
            close(s);
        }
    }
}

bool peer::output_idle(const session& s) {
    return s.output_offset >= s.output.size() && s.queued.empty() && !s.writing && !s.completion_posted;
}

void peer::send(session& s, const std::string& body) {
    if (!o_.serial_writes)
    {
        append_frame(s.output, body);
        flush(s);
        return;
    }
    std::string wire;
    append_frame(wire, body);
    s.queued.push_back(std::move(wire));
    start_write(s);
}

// Serial read pacing mirrors async_read_until on a 4096-byte streambuf: a frame
// already buffered completes through a posted bounce without recv; otherwise one
// recv is attempted, and its completion is posted to a later pass, where the
// matcher runs again. Would-block waits for one readiness event, which performs
// one recv. Each completion handles at most one frame, then starts the next read.
void peer::read_step(session& s) {
    if (s.fd < 0 || s.read_waiting || s.read_posted)
    {
        return;
    }
    const auto view = inspect(s.input);
    if (view.status == frame_status::invalid)
    {
        fail("read frame", "invalid or oversized dgram prefix");
        return;
    }
    if (view.status == frame_status::complete)
    {
        s.read_posted = true;
        post(s, true);
        return;
    }
    std::array<char, max_wire_bytes> buffer{};
    const auto room = max_wire_bytes - s.input.size();
    const auto received = ::recv(s.fd, buffer.data(), room, 0);
    if (received < 0)
    {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            ++histogram_.read.would_block;
            s.read_waiting = true;
            return;
        }
        if (!failed_)
        {
            fail("recv", std::strerror(errno));
        }
        close(s);
        return;
    }
    if (received == 0)
    {
        eof(s);
        return;
    }
    histogram_.read.record(room, static_cast<std::size_t>(received));
    s.bytes_read += static_cast<std::uint64_t>(received);
    s.last_read_at = latest_read_ = clock_type::now();
    s.input.append(buffer.data(), static_cast<std::size_t>(received));
    s.read_posted = true;
    post(s, true);
}

void peer::read_resume(session& s) {
    s.read_posted = false;
    if (s.fd < 0)
    {
        return;
    }
    const auto view = inspect(s.input);
    if (view.status == frame_status::complete)
    {
        frame(s, std::string_view(s.input).substr(view.prefix, view.size));
        if (s.fd < 0)
        {
            return;
        }
        s.input.erase(0, view.prefix + view.size);
    }
    read_step(s);
}

// A readiness event with no read waiting is consumed, as with an empty Asio op queue.
void peer::read_ready(session& s) {
    if (!o_.serial_reads)
    {
        readable(s);
        return;
    }
    if (s.read_waiting)
    {
        s.read_waiting = false;
        read_step(s);
    }
}

void peer::readable(session& s) {
    // Mirrors the native control's 4096-byte bounded input buffer.
    std::array<char, max_wire_bytes> buffer{};
    while (s.fd >= 0)
    {
        const auto room = max_wire_bytes - s.input.size();
        if (!room)
        {
            fail("read frame", "input buffer full without a complete frame");
            return;
        }
        const auto received = ::recv(s.fd, buffer.data(), room, 0);
        if (received < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            {
                ++histogram_.read.would_block;
                return;
            }
            if (!failed_)
            {
                fail("recv", std::strerror(errno));
            }
            close(s);
            return;
        }
        if (received == 0)
        {
            eof(s);
            return;
        }
        histogram_.read.record(room, static_cast<std::size_t>(received));
        s.bytes_read += static_cast<std::uint64_t>(received);
        s.last_read_at = latest_read_ = clock_type::now();
        s.input.append(buffer.data(), static_cast<std::size_t>(received));
        std::size_t consumed{};
        while (s.fd >= 0)
        {
            const auto view = inspect(std::string_view(s.input).substr(consumed));
            if (view.status == frame_status::invalid)
            {
                fail("read frame", "invalid or oversized dgram prefix");
                return;
            }
            if (view.status == frame_status::incomplete)
            {
                break;
            }
            frame(s, std::string_view(s.input).substr(consumed + view.prefix, view.size));
            consumed += view.prefix + view.size;
        }
        s.input.erase(0, consumed);
    }
}

void peer::frame(session& s, std::string_view body) {
    connection_load::frame_header h;
    if (!connection_load::decode(body, h))
    {
        fail("protocol", "frame decode failed");
        return;
    }
    if (client())
    {
        client_frame(s, h);
    }
    else
    {
        server_frame(s, h);
    }
}

void peer::client_frame(session& s, const connection_load::frame_header& h) {
    if (h.owner != s.owner)
    {
        fail("protocol", "identity mismatch");
    }
    else if (h.kind == frame_kind::hello_ack && !s.hello && !h.sequence)
    {
        s.hello = true;
        s.pending_since = {};
        ++ready_;
    }
    else if (h.kind == frame_kind::start_ack && s.hello && !s.started && !h.sequence)
    {
        s.started = true;
        s.pending_since = {};
        ++started_;
    }
    else if (h.kind == frame_kind::data && s.started && !s.finished && h.body_bytes == o_.payload &&
             h.sequence == s.next_receive && h.sequence < s.next_send)
    {
        ++s.next_receive;
        ++data_received_;
        --outstanding_;
        s.pending_since =
            s.next_receive < s.next_send ? s.sent_at[(s.next_receive - 1) % o_.window] : clock_type::time_point{};
        fill(s);
        finish(s);
    }
    else if (h.kind == frame_kind::finish_ack && s.started && s.finish_sent && !s.finished &&
             h.sequence == s.next_send - 1)
    {
        s.finished = true;
        s.pending_since = {};
        ++finished_;
    }
    else
    {
        fail("protocol", "unexpected phase or sequence");
    }
}

void peer::server_frame(session& s, const connection_load::frame_header& h) {
    if (h.kind == frame_kind::hello && !s.hello && h.owner.run == o_.run && h.owner.client && h.owner.connection &&
        h.sequence == 1)
    {
        if (!identities_.emplace(h.owner.client, h.owner.connection).second)
        {
            fail("protocol", "duplicate client/connection identity");
            return;
        }
        s.owner = h.owner;
        s.hello = true;
        ++ready_;
        send(s, connection_load::make_frame(frame_kind::hello_ack, s.owner));
    }
    else if (h.owner != s.owner || !s.hello)
    {
        fail("protocol", "identity mismatch");
    }
    else if (h.kind == frame_kind::start && !s.started && !h.sequence)
    {
        s.started = true;
        ++started_;
        send(s, connection_load::make_frame(frame_kind::start_ack, s.owner));
    }
    else if (h.kind == frame_kind::data && s.started && !s.finished && h.body_bytes == o_.payload &&
             h.sequence == s.next_receive)
    {
        ++s.next_receive;
        ++data_received_;
        send(s, connection_load::make_frame(frame_kind::data, s.owner, h.sequence, o_.payload));
        ++data_sent_;
    }
    else if (h.kind == frame_kind::finish && s.started && !s.finished && h.sequence == s.next_receive - 1)
    {
        s.finished = true;
        ++finished_;
        send(s, connection_load::make_frame(frame_kind::finish_ack, s.owner, h.sequence));
    }
    else
    {
        fail("protocol", "unexpected phase or sequence");
    }
}

void peer::fill(session& s) {
    while (traffic_ && !draining_ && !failed_ && s.fd >= 0 && s.next_send - s.next_receive < o_.window)
    {
        const auto sequence = s.next_send++;
        const auto now = clock_type::now();
        s.sent_at[(sequence - 1) % o_.window] = now;
        if (sequence == s.next_receive)
        {
            s.pending_since = now;
        }
        ++outstanding_;
        ++data_sent_;
        send(s, connection_load::make_frame(frame_kind::data, s.owner, sequence, o_.payload));
    }
}

void peer::finish(session& s) {
    if (draining_ && !failed_ && s.fd >= 0 && !s.finish_sent && s.next_receive == s.next_send)
    {
        s.finish_sent = true;
        s.pending_since = clock_type::now();
        send(s, connection_load::make_frame(frame_kind::finish, s.owner, s.next_send - 1));
    }
}

void peer::eof(session& s) {
    if (!client() && s.finished)
    {
        s.peer_eof = true;
        if (output_idle(s))
        {
            close(s);
        }
        return;
    }
    if (!failed_)
    {
        // Capture every unfinished peer before this EOF closes anything else.
        snapshot("before_unfinished_close", clock_type::now());
        fail("read", "peer closed before finish");
    }
    close(s);
}

void peer::close(session& s) {
    if (s.fd < 0)
    {
        return;
    }
    if (s.connecting)
    {
        s.connecting = false;
        --connecting_;
    }
    ::close(s.fd);
    s.fd = -1;
    s.kevents = 0;
    s.queued.clear();
    s.writing = false;
    ++disconnected_;
}

void peer::close_all() {
    for (auto& s : sessions_)
    {
        close(s);
    }
}

void peer::fail(const std::string& scope, const std::string& reason) {
    if (failed_)
    {
        return;
    }
    failed_ = true;
    failed_at_ = clock_type::now();
    failure_scope_ = scope;
    failure_reason_ = reason;
}

void peer::snapshot(const char* phase, clock_type::time_point now) {
    snapshot_taken_ = true;
    for (auto& s : sessions_)
    {
        const bool stalled = s.pending_since != clock_type::time_point{} &&
                             now - s.pending_since >= std::chrono::milliseconds(o_.request_ms);
        // The server cannot see request age; it reports unfinished peers whose reads
        // stopped at least request-timeout-ms before its most recent read anywhere.
        const bool quiet = latest_read_ - s.last_read_at >= std::chrono::milliseconds(o_.request_ms);
        if (client() ? !(s.watched || stalled) : (s.finished || !quiet))
        {
            continue;
        }
        s.watched = true;
        const bool open = s.fd >= 0;
        out_ << "{\"event\":\"stalled_connection\",\"tool\":\"poll_control\",\"role\":\"" << o_.role
             << "\",\"phase\":\"" << phase << "\",\"elapsed_ms\":" << elapsed_ms(began_, now)
             << ",\"client\":" << s.owner.client << ",\"connection\":" << s.owner.connection
             << ",\"open\":" << (open ? "true" : "false") << ",\"local_port\":" << (open ? port_of(s.fd, false) : -1)
             << ",\"peer_port\":" << (open ? port_of(s.fd, true) : -1) << ",\"next_send\":" << s.next_send
             << ",\"next_receive\":" << s.next_receive << ",\"hello\":" << (s.hello ? "true" : "false")
             << ",\"started\":" << (s.started ? "true" : "false")
             << ",\"finish_sent\":" << (s.finish_sent ? "true" : "false") << ",\"pending_ms\":"
             << (s.pending_since == clock_type::time_point{} ? -1
                                                             : static_cast<long long>(elapsed_ms(s.pending_since, now)))
             << ",\"app_unsent_bytes\":" << (s.output.size() - s.output_offset)
             << ",\"app_buffered_input_bytes\":" << s.input.size() << ",\"bytes_written\":" << s.bytes_written
             << ",\"bytes_read\":" << s.bytes_read << ",\"read_idle_ms\":"
             << (s.last_read_at == clock_type::time_point{} ? -1
                                                            : static_cast<long long>(elapsed_ms(s.last_read_at, now)))
             << ",\"kernel_readable_bytes\":" << (open ? readable_bytes(s.fd) : -1)
             << ",\"kernel_send_queue_bytes\":" << (open ? send_queue_bytes(s.fd) : -1) << ",\"tcp_info\":";
        write_tcp_info(out_, s.fd);
        out_ << "}\n";
    }
    out_ << std::flush;
}

// Opt-in: every 250 ms query each open socket's kernel send queue. A queue that
// stays non-empty and unchanged for one second has made no delivery progress on
// loopback; report it then once per second (at most 20 reports per session and 64
// sessions), and report recovery once it changes. Reports carry Apple TCP info.
void peer::watch_send_queues(clock_type::time_point now) {
    if (!o_.send_queue_watch || now < next_watch_)
    {
        return;
    }
    next_watch_ = now + std::chrono::milliseconds(250);
    for (auto& s : sessions_)
    {
        if (s.fd < 0 || s.connecting)
        {
            continue;
        }
        const auto queued = send_queue_bytes(s.fd);
        const bool reported = s.send_queue_reports > 0;
        if (queued <= 0 || queued != s.send_queue_last || s.send_queue_since == clock_type::time_point{})
        {
            if (reported && queued != s.send_queue_last)
            {
                emit_send_queue(s, "progressed", now, queued);
                s.send_queue_reports = 0;
                --send_queue_tracked_;
            }
            s.send_queue_last = queued;
            s.send_queue_since = queued > 0 ? now : clock_type::time_point{};
            continue;
        }
        const auto unchanged = now - s.send_queue_since;
        if (unchanged < std::chrono::seconds(1) || s.send_queue_reports >= 20 ||
            (!reported && send_queue_tracked_ >= 64))
        {
            continue;
        }
        // Report on the first detection, then once per further second unchanged.
        if (reported && unchanged < std::chrono::seconds(1 + s.send_queue_reports))
        {
            continue;
        }
        if (!reported)
        {
            ++send_queue_tracked_;
        }
        ++s.send_queue_reports;
        emit_send_queue(s, reported ? "sample" : "detected", now, queued);
    }
    out_ << std::flush;
}

void peer::emit_send_queue(const session& s, const char* phase, clock_type::time_point now, long long queued) {
    out_ << "{\"event\":\"send_queue_stalled\",\"tool\":\"poll_control\",\"role\":\"" << o_.role << "\",\"phase\":\""
         << phase << "\",\"elapsed_ms\":" << elapsed_ms(began_, now) << ",\"client\":" << s.owner.client
         << ",\"connection\":" << s.owner.connection << ",\"local_port\":" << port_of(s.fd, false)
         << ",\"peer_port\":" << port_of(s.fd, true) << ",\"unchanged_ms\":"
         << (s.send_queue_since == clock_type::time_point{} ? 0 : elapsed_ms(s.send_queue_since, now))
         << ",\"kernel_send_queue_bytes\":" << queued << ",\"previous_send_queue_bytes\":" << s.send_queue_last
         << ",\"kernel_readable_bytes\":" << readable_bytes(s.fd) << ",\"next_send\":" << s.next_send
         << ",\"next_receive\":" << s.next_receive << ",\"finished\":" << (s.finished ? "true" : "false")
         << ",\"app_unsent_bytes\":" << (s.output.size() - s.output_offset)
         << ",\"app_queued_frames\":" << s.queued.size() << ",\"bytes_written\":" << s.bytes_written
         << ",\"bytes_read\":" << s.bytes_read << ",\"tcp_info\":";
    write_tcp_info(out_, s.fd);
    out_ << "}\n";
}

bool peer::complete() const {
    if (client())
    {
        return finished_ == o_.connections && !outstanding_ &&
               std::all_of(sessions_.begin(), sessions_.end(), [](const session& s) { return output_idle(s); });
    }
    return finished_ == o_.expected && disconnected_ == o_.expected;
}

void peer::advance(clock_type::time_point now) {
    if (signalled && !failed_)
    {
        snapshot("on_signal", now);
        fail("signal", "SIGINT or SIGTERM");
    }
    if (failed_)
    {
        return;
    }
    if (now - began_ >= std::chrono::duration<double>(o_.deadline))
    {
        snapshot("at_deadline", now);
        fail("deadline", "workload exceeded deadline-seconds");
        return;
    }
    if (!client())
    {
        return;
    }
    const auto interval = std::chrono::nanoseconds(1000000000ULL / o_.rate);
    while (!failed_ && attempted_ < o_.connections && connecting_ < o_.parallel && now >= next_launch_)
    {
        launch();
        next_launch_ += interval;
    }
    if (!starting_ && ready_ == o_.connections)
    {
        starting_ = true;
        for (auto& s : sessions_)
        {
            s.pending_since = now;
            send(s, connection_load::make_frame(frame_kind::start, s.owner));
        }
    }
    if (!traffic_ && started_ == o_.connections)
    {
        traffic_ = true;
        until_ = now + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(o_.hold));
        for (auto& s : sessions_)
        {
            fill(s);
        }
    }
    if (traffic_ && !draining_ && now >= until_)
    {
        draining_ = true;
        for (auto& s : sessions_)
        {
            finish(s);
        }
    }
    for (auto& s : sessions_)
    {
        if (s.connecting && now - s.connect_since >= std::chrono::milliseconds(o_.connect_ms))
        {
            ++timeouts_;
            fail("connect timeout", "pending connect exceeded connect-timeout-ms");
            return;
        }
        if (s.pending_since != clock_type::time_point{} &&
            now - s.pending_since >= std::chrono::milliseconds(o_.request_ms))
        {
            ++timeouts_;
            snapshot("at_failure", now);
            fail("request timeout", "pending response exceeded request-timeout-ms");
            return;
        }
    }
}

void peer::result(clock_type::time_point now) const {
    out_ << "{\"event\":\"result\",\"tool\":\"poll_control\",\"role\":\"" << o_.role << "\",\"events\":\""
         << (o_.kqueue_events ? "kqueue" : "poll") << "\",\"socket_setup\":\""
         << (o_.asio_socket_setup ? "asio" : "default")
         << "\",\"send_queue_watch\":" << (o_.send_queue_watch ? "true" : "false") << ",\"read_pacing\":\""
         << (o_.serial_reads ? "serial" : "drain") << "\",\"write_pacing\":\""
         << (o_.serial_writes ? "serial" : "burst") << "\",\"success\":" << (failed_ ? "false" : "true")
         << ",\"failure_scope\":\"" << failure_scope_ << "\",\"failure_reason\":\"" << failure_reason_
         << "\",\"elapsed_ms\":" << elapsed_ms(began_, now) << ",\"expected\":" << o_.expected
         << ",\"attempted\":" << attempted_ << ",\"established\":" << established_ << ",\"accepted\":" << accepted_
         << ",\"ready\":" << ready_ << ",\"started\":" << started_ << ",\"finished\":" << finished_
         << ",\"disconnected\":" << disconnected_ << ",\"data_sent\":" << data_sent_
         << ",\"data_received\":" << data_received_ << ",\"outstanding\":" << outstanding_
         << ",\"timeouts\":" << timeouts_ << ",\"io_histogram\":";
    histogram_.write_json(out_);
    out_ << "}\n" << std::flush;
}

void peer::wait_poll() {
    descriptors_.clear();
    owners_.clear();
    if (listener_ >= 0 && !failed_)
    {
        descriptors_.push_back({listener_, POLLIN, 0});
        owners_.push_back(sessions_.size());
    }
    for (std::size_t index = 0; index < sessions_.size(); ++index)
    {
        const auto& s = sessions_[index];
        if (s.fd < 0)
        {
            continue;
        }
        // Serial reads ask for POLLIN only while waiting, so level triggering cannot spin.
        short events = s.connecting ? POLLOUT : (!o_.serial_reads || s.read_waiting ? POLLIN : 0);
        if (s.output_offset < s.output.size())
        {
            events |= POLLOUT;
        }
        descriptors_.push_back({s.fd, events, 0});
        owners_.push_back(index);
    }
    // Posted completions make the wait non-blocking, like Asio's reactor with ready handlers.
    const auto ready = ::poll(descriptors_.data(), static_cast<nfds_t>(descriptors_.size()), posted_.empty() ? 5 : 0);
    if (ready < 0)
    {
        if (errno != EINTR)
        {
            fail("poll", std::strerror(errno));
        }
        return;
    }
    ++histogram_.waits;
    histogram_.events += static_cast<std::uint64_t>(ready);
    // Accept last: it may append to sessions_ and owners_ index the old vector.
    bool accept_ready = false;
    for (std::size_t index = 0; index < descriptors_.size() && ready > 0; ++index)
    {
        const auto revents = descriptors_[index].revents;
        if (!revents)
        {
            continue;
        }
        if (owners_[index] == sessions_.size())
        {
            accept_ready = true;
            continue;
        }
        auto& s = sessions_[owners_[index]];
        if (s.connecting)
        {
            connect_ready(s);
            continue;
        }
        if (revents & POLLOUT)
        {
            flush(s);
        }
        if (s.fd >= 0 && (revents & (POLLIN | POLLHUP | POLLERR)))
        {
            read_ready(s);
        }
    }
    if (accept_ready)
    {
        accept_all();
    }
}

#if ARKNET_POLL_CONTROL_HAS_KQUEUE
// Mirrors Boost.Asio's kqueue reactor rather than an idealized kqueue loop:
// send/recv/accept/connect are attempted first; only an operation that must wait
// registers its descriptor, with EV_ADD | EV_CLEAR, READ alone for reads and READ
// plus WRITE once a write or connect must wait. A WRITE event that arrives with
// nothing left to write deletes the WRITE registration. One kevent call returns at
// most 128 events. udata is the session index plus one; zero is the listener.
void peer::register_events(std::uintptr_t id, int fd, int count, int& registered) {
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, reinterpret_cast<void*>(id));
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, reinterpret_cast<void*>(id));
    if (::kevent(kqueue_, changes, count, nullptr, 0, nullptr) != 0)
    {
        fail("kevent register", std::strerror(errno));
        return;
    }
    registered = count;
}

void peer::wait_kqueue() {
    if (kqueue_ < 0)
    {
        kqueue_ = ::kqueue();
        if (kqueue_ < 0)
        {
            fail("kqueue", std::strerror(errno));
            return;
        }
    }
    if (listener_ >= 0 && !listener_registered_)
    {
        int registered{};
        register_events(0, listener_, 1, registered);
        listener_registered_ = registered != 0;
    }
    for (std::size_t index = 0; index < sessions_.size() && !failed_; ++index)
    {
        auto& s = sessions_[index];
        const int wanted = s.connecting || s.output_offset < s.output.size() ? 2 : 1;
        if (s.fd >= 0 && s.kevents < wanted)
        {
            register_events(index + 1, s.fd, wanted, s.kevents);
        }
    }
    std::array<struct kevent, 128> events{};
    const timespec timeout{0, posted_.empty() ? 5000000 : 0};
    const auto count = ::kevent(kqueue_, nullptr, 0, events.data(), static_cast<int>(events.size()), &timeout);
    if (count < 0)
    {
        if (errno != EINTR)
        {
            fail("kevent wait", std::strerror(errno));
        }
        return;
    }
    ++histogram_.waits;
    histogram_.events += static_cast<std::uint64_t>(count);
    bool accept_ready = false;
    for (int event = 0; event < count; ++event)
    {
        const auto& value = events[static_cast<std::size_t>(event)];
        const auto id = reinterpret_cast<std::uintptr_t>(value.udata);
        if (!id)
        {
            accept_ready = true;
            continue;
        }
        // Closing a descriptor removes its registrations; skip events already fetched.
        auto& s = sessions_[id - 1];
        if (s.fd < 0)
        {
            continue;
        }
        if (value.flags & EV_ERROR)
        {
            fail("kevent event", std::strerror(static_cast<int>(value.data)));
            continue;
        }
        if (value.filter == EVFILT_WRITE && s.kevents == 2 && !s.connecting && s.output_offset >= s.output.size())
        {
            struct kevent change;
            EV_SET(&change, s.fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            ::kevent(kqueue_, &change, 1, nullptr, 0, nullptr);
            s.kevents = 1;
        }
        if (s.connecting)
        {
            connect_ready(s);
        }
        else if (value.filter == EVFILT_WRITE)
        {
            flush(s);
        }
        else if (value.filter == EVFILT_READ)
        {
            read_ready(s);
        }
    }
    if (accept_ready)
    {
        accept_all();
    }
}
#endif

int peer::run() {
    if (!client())
    {
        open_listener();
    }
    while (true)
    {
        auto now = clock_type::now();
        advance(now);
        if (!failed_ && complete())
        {
            break;
        }
        // After the first request timeout the client admits nothing new but keeps
        // reading for --observe-seconds, then reports the same connections again.
        if (failed_ && (!client() || failure_scope_ != "request timeout" ||
                           now - failed_at_ >= std::chrono::duration<double>(o_.observe)))
        {
            if (client() && failure_scope_ == "request timeout" && o_.observe > 0)
            {
                snapshot("after_observation", now);
            }
            break;
        }
        watch_send_queues(now);
        run_posted();
#if ARKNET_POLL_CONTROL_HAS_KQUEUE
        if (o_.kqueue_events)
        {
            wait_kqueue();
            continue;
        }
#endif
        wait_poll();
    }
    const auto now = clock_type::now();
    close_all();
    result(now);
    return failed_ ? 1 : 0;
}

void raise_descriptor_limit(std::uint64_t connections) {
    rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0)
    {
        return;
    }
    const auto wanted = static_cast<rlim_t>(connections + 64);
    if (limit.rlim_cur < wanted)
    {
        limit.rlim_cur = (std::min)(wanted, limit.rlim_max);
        ::setrlimit(RLIMIT_NOFILE, &limit);
    }
}

void require(bool condition, const char* message) {
    if (!condition)
    {
        throw std::runtime_error(std::string("self-test failed: ") + message);
    }
}

void self_test() {
    std::string wire;
    append_frame(wire, std::string(1088, 'x'));
    require(wire.size() == 1091 && static_cast<unsigned char>(wire[0]) == 254, "three-byte prefix");
    require(inspect(wire).status == frame_status::complete && inspect(wire).size == 1088, "complete frame");
    require(inspect(std::string_view(wire).substr(0, 2)).status == frame_status::incomplete, "partial prefix");
    require(inspect(std::string_view(wire).substr(0, 100)).status == frame_status::incomplete, "partial body");
    require(inspect(std::string("\xfe\x05\x00", 3)).status == frame_status::invalid, "non-canonical prefix");
    require(inspect(std::string("\xfe\xff\x0f", 3)).status == frame_status::invalid, "oversized frame");
    bool rejected = false;
    try
    { parse({"--role", "server", "--listen", "127.0.0.1", "--port", "1", "--connections", "1"}); }
    catch (const std::invalid_argument&)
    { rejected = true; }
    require(rejected, "role-inappropriate option");

    std::vector<std::string> modes{"poll"};
    if (ARKNET_POLL_CONTROL_HAS_KQUEUE)
    {
        modes.push_back("kqueue");
    }
    std::size_t runs{};
    for (const auto& mode : modes)
    {
        for (const std::string pacing : {"burst", "serial"})
        {
            // Serial reads are exercised with serial writes, the native-control-like pairing.
            const std::string reads = pacing == "serial" ? "serial" : "drain";
            const std::string setup = pacing == "serial" ? "asio" : "default";
            const std::string watch = pacing == "serial" ? "on" : "off";
            ++runs;
            std::atomic<int> port{0};
            std::ostringstream server_out, client_out;
            int server_status = -1;
            std::thread server([&] {
                auto o = parse({"--role", "server", "--listen", "127.0.0.1", "--port", "0", "--expected-connections",
                    "8", "--deadline-seconds", "10", "--events", mode, "--write-pacing", pacing, "--read-pacing", reads,
                    "--socket-setup", setup, "--send-queue-watch", watch});
                server_status = peer(o, server_out, &port).run();
            });
            while (!port.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            auto o = parse(
                {"--role", "client", "--target", "127.0.0.1", "--port", std::to_string(port.load()), "--connections",
                    "8", "--hold-seconds", "0.2", "--deadline-seconds", "10", "--events", mode, "--write-pacing",
                    pacing, "--read-pacing", reads, "--socket-setup", setup, "--send-queue-watch", watch});
            const auto client_status = peer(o, client_out).run();
            server.join();
            require(client_status == 0 && server_status == 0, "loopback traffic run");
            require(client_out.str().find("\"success\":true") != std::string::npos, "client result");
            require(client_out.str().find("\"events\":\"" + mode + "\",\"socket_setup\":\"" + setup +
                                          "\",\"send_queue_watch\":" + (watch == "on" ? "true" : "false") +
                                          ",\"read_pacing\":\"" + reads + "\",\"write_pacing\":\"" + pacing + "\"") !=
                        std::string::npos,
                "event mode and pacing result");
            // A healthy loopback run drains every send queue well within the one-second threshold.
            require(server_out.str().find("send_queue_stalled") == std::string::npos &&
                        client_out.str().find("send_queue_stalled") == std::string::npos,
                "no send-queue stall on a healthy run");
        }
    }
    std::cout << "poll control self-test passed (" << runs << " event/pacing combinations)\n";
}

void usage() {
    std::cout << "arknet_poll_connection_control: poll(2) or kqueue TCP peer without Asio or strands.\n"
                 "Speaks the arknet_asio_connection_control traffic protocol; either role may be paired with it.\n\n"
                 "  --role server --listen IPV4 --port N --expected-connections N [--payload N]\n"
                 "  --role client --target IPV4 --port N --connections N [--client-id N] [--payload N]\n"
                 "Common: --run-id N --window N --connect-rate N --connect-parallel N --connect-timeout-ms N\n"
                 "        --request-timeout-ms N --hold-seconds S --deadline-seconds S\n"
                 "Client: --observe-seconds S keeps reading after the first request timeout, then reports again.\n"
                 "  --events poll|kqueue  default poll; kqueue (Apple/BSD) mirrors Boost.Asio's kqueue reactor\n"
                 "  --write-pacing burst|serial  default burst; serial writes one frame per session at a time and\n"
                 "        starts the next on a later loop pass, like the native control's serialized async_write\n"
                 "  --socket-setup default|asio  asio sets SO_NOSIGPIPE and FIONBIO like Boost.Asio's socket_ops\n"
                 "  --send-queue-watch off|on  default off; on reports kernel send queues unchanged for 1 s,\n"
                 "        with Apple TCP_CONNECTION_INFO, every 250 ms scan\n"
                 "  --read-pacing drain|serial  default drain; serial performs one recv per step and handles one\n"
                 "        frame per posted completion, like the native control's async_read_until\n"
                 "  --self-test   run codec, option and eight-connection loopback checks in each mode\n";
}

} // namespace

int main(int argc, char** argv) {
    try
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            usage();
            return 0;
        }
        std::signal(SIGPIPE, SIG_IGN);
        if (argc == 2 && std::string_view(argv[1]) == "--self-test")
        {
            self_test();
            return 0;
        }
        const auto o = parse(std::vector<std::string>(argv + 1, argv + argc));
        raise_descriptor_limit(o.expected);
        std::signal(SIGINT, [](int) { signalled = 1; });
        std::signal(SIGTERM, [](int) { signalled = 1; });
        return peer(o, std::cout).run();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 2;
    }
}
