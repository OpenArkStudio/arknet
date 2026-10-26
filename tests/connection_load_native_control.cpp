// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../benchmarks/connections_protocol.hpp"

namespace {

using clock_type = std::chrono::steady_clock;
std::string directory;
unsigned serial{};

std::string read_file(const std::string& path) {
    std::ifstream input(path);
    return std::string(std::istreambuf_iterator<char>(input), {});
}

struct child
{
    pid_t pid = -1;
    std::string log;
    bool reaped{};
    int exit_code = -1;

    child(const std::string& executable, std::vector<std::string> arguments) {
        log = directory + "/" + std::to_string(++serial) + ".log";
        arguments.insert(arguments.begin(), executable);
        std::vector<char*> argv;
        for (auto& value : arguments)
        {
            argv.push_back(value.data());
        }
        argv.push_back(nullptr);
        pid = fork();
        if (pid == -1)
        {
            throw std::runtime_error("fork failed");
        }
        if (!pid)
        {
            const auto fd = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            if (fd == -1 || dup2(fd, STDOUT_FILENO) == -1 || dup2(fd, STDERR_FILENO) == -1)
            {
                std::_Exit(127);
            }
            close(fd);
            execv(executable.c_str(), argv.data());
            std::_Exit(127);
        }
    }

    ~child() {
        if (!reaped && pid > 0)
        {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR)
            {
            }
        }
    }

    bool poll() {
        if (reaped)
        {
            return true;
        }
        int status{};
        const auto result = waitpid(pid, &status, WNOHANG);
        if (result == -1 && errno != EINTR)
        {
            throw std::runtime_error("waitpid failed");
        }
        if (result == pid)
        {
            reaped = true;
            exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
        }
        return reaped;
    }

    int wait() {
        const auto until = clock_type::now() + std::chrono::seconds(8);
        while (clock_type::now() < until)
        {
            if (poll())
            {
                std::ofstream(log + ".exit") << exit_code << '\n';
                return exit_code;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("child did not terminate: " + log);
    }

    std::string port() {
        const auto until = clock_type::now() + std::chrono::seconds(2);
        while (clock_type::now() < until)
        {
            const auto output = read_file(log);
            const auto offset = output.find("\"port\":");
            if (offset != std::string::npos)
            {
                const auto begin = offset + 7;
                const auto end = output.find_first_not_of("0123456789", begin);
                const auto value = output.substr(begin, end - begin);
                if (!value.empty())
                {
                    return value;
                }
            }
            if (poll())
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        throw std::runtime_error("server did not become ready: " + log + " " + read_file(log));
    }
};

std::uint64_t counter(const std::string& line, const std::string& name) {
    const auto key = "\"" + name + "\":";
    const auto offset = line.find(key);
    if (offset == std::string::npos)
    {
        throw std::runtime_error("missing counter " + name + ": " + line);
    }
    const auto begin = line.data() + offset + key.size();
    std::uint64_t value{};
    const auto parsed = std::from_chars(begin, line.data() + line.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr == line.data() + line.size() ||
        (*parsed.ptr != ',' && *parsed.ptr != '}'))
    {
        throw std::runtime_error("invalid counter " + name + ": " + line);
    }
    return value;
}

void sample_phases(const std::string& path, unsigned count, bool native) {
    std::istringstream lines(read_file(path));
    std::uint64_t previous_ready{}, previous_started{}, previous_finished{};
    bool before_start{}, previous_idle{}, idle_interval{}, complete{};
    unsigned samples{};
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.find("\"event\":\"sample\"") != std::string::npos)
        {
            ++samples;
            const auto ready = counter(line, "ready");
            const auto started = counter(line, "started");
            const auto finished = counter(line, "finished");
            if (ready < previous_ready || started < previous_started || finished < previous_finished || ready > count ||
                started > count || finished > count)
            {
                throw std::runtime_error("nonmonotonic phase counter: " + path + " " + line);
            }
            if (native && counter(line, "sample_interval_ms") != 1000)
            {
                throw std::runtime_error("native sampling cadence changed: " + path);
            }
            if (line.find("\"resource_cpu_interval_s\":") == std::string::npos)
            {
                throw std::runtime_error("missing CPU interval evidence: " + path);
            }
            before_start = before_start || (ready < count && !started && !finished);
            const auto idle = counter(line, "active") == count && ready == count && started == count && !finished &&
                              !counter(line, "pending_send_bytes");
            idle_interval = idle_interval || (previous_idle && idle);
            previous_idle = idle;
            previous_ready = ready;
            previous_started = started;
            previous_finished = finished;
        }
        else if (line.find("\"event\":\"result\"") != std::string::npos)
        {
            complete = counter(line, "ready") == count && counter(line, "started") == count &&
                       counter(line, "finished") == count;
        }
    }
    if (!before_start || !idle_interval || !complete)
    {
        throw std::runtime_error("missing handshake/idle interval/finish evidence: " + path + "\n" + read_file(path));
    }
    std::cout << "phase samples=" << samples << " include consecutive idle endpoints and completed FIN: " << path
              << '\n';
}

void pair(const std::string& server_binary, const std::string& client_binary, unsigned count,
    const std::string& scenario = "traffic", unsigned payload = 1024, bool phases = false, bool trace = false) {
    std::vector<std::string> common{"--run-id", "71", "--scenario", scenario, "--payload", std::to_string(payload),
        "--window", "4", "--max-connections", std::to_string(count), "--hold-seconds", phases ? "3.2" : "0.03",
        "--deadline-seconds", phases ? "7" : "5", "--cleanup-seconds", "1"};
    if (trace)
    {
        common.insert(common.end(), {"--operation-trace", "on"});
    }
    auto server_args = common;
    server_args.insert(server_args.end(),
        {"--role", "server", "--listen", "127.0.0.1", "--port", "0", "--expected-connections", std::to_string(count)});
    child server(server_binary, server_args);
    auto client_args = common;
    client_args.insert(
        client_args.end(), {"--role", "client", "--target", "127.0.0.1", "--port", server.port(), "--client-id", "91",
                               "--connections", std::to_string(count), "--connect-rate", phases ? "1" : "1000"});
    child client(client_binary, client_args);
    const auto client_status = client.wait();
    const auto server_status = server.wait();
    if (client_status || server_status)
    {
        throw std::runtime_error(
            "pair failed: " + client.log + " " + server.log + "\n" + read_file(client.log) + read_file(server.log));
    }
    for (const auto& path : {client.log, server.log})
    {
        const auto output = read_file(path);
        if (output.find("\"event\":\"result\"") == std::string::npos ||
            output.find("\"finished\":" + std::to_string(count)) == std::string::npos)
        {
            throw std::runtime_error("missing final target evidence: " + path);
        }
        if (output.find("arknet_asio_connection_control") != std::string::npos &&
            (output.find("\"success\":true") == std::string::npos ||
                output.find("\"cleanup_complete\":true") == std::string::npos ||
                output.find("\"controller_on_io_worker\":false") == std::string::npos ||
                output.find("\"initial_buffer_bytes\":4096") == std::string::npos))
        {
            throw std::runtime_error("native terminal/cleanup evidence missing: " + path);
        }
        if (trace &&
            (output.find("\"operation_trace_enabled\":true,\"operation_trace_emitted\":false") == std::string::npos ||
                output.find("\"event\":\"operation_trace\"") != std::string::npos))
        {
            throw std::runtime_error("successful opt-in run emitted a failure trace: " + path);
        }
        if (!trace && output.find("operation_trace") != std::string::npos)
        {
            throw std::runtime_error("default run changed diagnostic output: " + path);
        }
        if (phases)
        {
            sample_phases(path, count, output.find("arknet_asio_connection_control") != std::string::npos);
        }
    }
    std::cout << "separate-process " << scenario << " connections=" << count << " payload=" << payload << " passed\n";
}

void invalid_arguments(const std::string& executable) {
    const std::vector<std::string> base{"--role", "client", "--target", "127.0.0.1", "--port", "1", "--run-id", "1"};
    for (const auto& suffix : std::vector<std::vector<std::string>>{{"--connections", "-1"}, {"--connections", "1x"},
             {"--io-threads", "2"}, {"--max-buffer-bytes", "8192"}, {"--sample-ms", "1000"}, {"--hold-seconds", "nan"},
             {"--hold-seconds", "inf"}, {"--port", "2"}, {"--unknown", "1"}, {"--connections"},
             {"--connections", "5000", "--window", "4", "--max-inflight", "19999"}, {"--operation-trace", "true"},
             {"--operation-trace", "ON"}, {"--operation-trace"},
             {"--operation-trace", "on", "--operation-trace", "off"}})
    {
        auto args = base;
        args.insert(args.end(), suffix.begin(), suffix.end());
        child value(executable, args);
        if (value.wait() != 1 || read_file(value.log).find("native control:") == std::string::npos)
        {
            throw std::runtime_error("invalid arguments not rejected: " + value.log);
        }
    }
    // A real asynchronous deadline must report incomplete target and actual drain.
    child server(executable,
        {"--role", "server", "--listen", "127.0.0.1", "--port", "0", "--run-id", "1", "--expected-connections", "1",
            "--hold-seconds", "0.01", "--deadline-seconds", "0.05", "--cleanup-seconds", "1"});
    server.port();
    const auto status = server.wait();
    const auto output = read_file(server.log);
    if (status != 1 || output.find("\"success\":false") == std::string::npos ||
        output.find("\"cleanup_complete\":true") == std::string::npos ||
        output.find("deadline: target incomplete") == std::string::npos)
    {
        throw std::runtime_error("deadline/cleanup evidence missing: " + server.log);
    }
}

void silent_peer(const std::string& executable, bool trace = false) {
    struct listener
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);

        ~listener() {
            if (fd != -1)
            {
                close(fd);
            }
        }
    } peer;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (peer.fd == -1 || fcntl(peer.fd, F_SETFD, FD_CLOEXEC) == -1 ||
        bind(peer.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1 || listen(peer.fd, 1) == -1)
    {
        throw std::runtime_error("silent peer setup failed");
    }
    socklen_t length = sizeof(address);
    if (getsockname(peer.fd, reinterpret_cast<sockaddr*>(&address), &length) == -1)
    {
        throw std::runtime_error("silent peer port lookup failed");
    }
    // The kernel accepts the connection, but no application replies to HELLO.
    std::vector<std::string> args{"--role", "client", "--target", "127.0.0.1", "--port",
        std::to_string(ntohs(address.sin_port)), "--run-id", "41", "--connections", "1", "--request-timeout-ms",
        trace ? "1200" : "100", "--hold-seconds", "0.01", "--deadline-seconds", "3", "--cleanup-seconds", "1"};
    if (trace)
    {
        args.insert(args.end(), {"--operation-trace", "on"});
    }
    child client(executable, args);
    const auto status = client.wait();
    const auto output = read_file(client.log);
    const auto snapshot = output.find("\"event\":\"connection_diagnostic\"");
    const auto result = output.find("\"event\":\"result\"");
    if (status != 1 || snapshot == std::string::npos || result == std::string::npos || snapshot >= result ||
        output.find("\"snapshot_phase\":\"before_socket_cancel\"") == std::string::npos ||
        output.find("\"connection_id\":1") == std::string::npos ||
        output.find("\"read_pending\":true") == std::string::npos ||
        output.find("\"last_write_kind\":1") == std::string::npos ||
        output.find("\"incomplete_sessions\":1,\"emitted\":1,\"omitted\":0") == std::string::npos ||
        output.find("\"cleanup_complete\":true") == std::string::npos ||
        output.find("request timeout:") == std::string::npos)
    {
        throw std::runtime_error("pre-cancel connection diagnostics missing: " + client.log);
    }
    const auto trace_position = output.find("\"event\":\"operation_trace\"");
    if (trace)
    {
        const auto summary = output.find("\"event\":\"operation_trace_summary\"");
        if (trace_position == std::string::npos || summary == std::string::npos || summary >= snapshot ||
            trace_position >= summary ||
            output.find("\"frozen\":true,\"history_complete\":true") == std::string::npos ||
            output.find("\"operation_scope\":\"composed_only\",\"low_level_operations\":false") == std::string::npos ||
            output.find("\"phase\":\"read_start\"") == std::string::npos ||
            output.find("\"phase\":\"write_complete\"") == std::string::npos ||
            output.find("\"phase\":\"read_complete\"") != std::string::npos ||
            output.find("\"output_truncated\":false") == std::string::npos ||
            output.find("\"emitted_sessions\":1,\"omitted_incomplete_sessions\":0") == std::string::npos ||
            output.find("\"operation_trace_enabled\":true,\"operation_trace_emitted\":true") == std::string::npos ||
            counter(output.substr(summary), "worker_markers_completed") < 1 || snapshot - trace_position > 256 * 1024)
        {
            throw std::runtime_error("bounded frozen trace/worker progress evidence missing: " + client.log);
        }
    }
    else if (output.find("operation_trace") != std::string::npos)
    {
        throw std::runtime_error("disabled diagnostics emitted a trace: " + client.log);
    }
}

void progress_then_stall(const std::string& executable) {
    struct raw_peer
    {
        int listener = socket(AF_INET, SOCK_STREAM, 0), connection = -1;

        ~raw_peer() {
            if (connection != -1)
            {
                close(connection);
            }
            if (listener != -1)
            {
                close(listener);
            }
        }
    } peer;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (peer.listener == -1 || fcntl(peer.listener, F_SETFD, FD_CLOEXEC) == -1 ||
        fcntl(peer.listener, F_SETFL, O_NONBLOCK) == -1 ||
        bind(peer.listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(peer.listener, 1))
    {
        throw std::runtime_error("kernel sample peer listener setup failed");
    }
    socklen_t length = sizeof(address);
    if (getsockname(peer.listener, reinterpret_cast<sockaddr*>(&address), &length))
    {
        throw std::runtime_error("kernel sample peer port query failed");
    }
    child client(executable,
        {"--role", "client", "--target", "127.0.0.1", "--port", std::to_string(ntohs(address.sin_port)), "--run-id",
            "97", "--client-id", "3", "--connections", "1", "--scenario", "traffic", "--payload", "1", "--window", "1",
            "--hold-seconds", "4", "--deadline-seconds", "6", "--request-timeout-ms", "3200", "--cleanup-seconds", "1",
            "--operation-trace", "on", "--tcp-kernel-samples", "on"});
    const auto deadline = clock_type::now() + std::chrono::seconds(6);
    auto wait_ready = [&](int fd, short events) {
        while (clock_type::now() < deadline)
        {
            pollfd descriptor{fd, events, 0};
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock_type::now());
            const auto wait_ms = remaining.count() > 0 ? remaining.count() : 0;
            const auto result = ::poll(&descriptor, 1, static_cast<int>(wait_ms) + 1);
            if (result > 0)
            {
                return;
            }
            if (result < 0 && errno != EINTR)
            {
                throw std::runtime_error("kernel sample peer poll failed");
            }
        }
        throw std::runtime_error("kernel sample peer IO deadline exceeded: " + client.log);
    };
    while (peer.connection == -1)
    {
        wait_ready(peer.listener, POLLIN);
        peer.connection = accept(peer.listener, nullptr, nullptr);
        if (peer.connection == -1 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            throw std::runtime_error("kernel sample peer accept failed");
        }
    }
    if (fcntl(peer.connection, F_SETFD, FD_CLOEXEC) == -1 || fcntl(peer.connection, F_SETFL, O_NONBLOCK) == -1)
    {
        throw std::runtime_error("kernel sample peer socket setup failed");
    }
#if defined(SO_NOSIGPIPE)
    const int no_sigpipe = 1;
    if (setsockopt(peer.connection, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)))
    {
        throw std::runtime_error("kernel sample peer SIGPIPE setup failed");
    }
#endif
    auto send_bytes = [&](std::string_view bytes) {
        while (!bytes.empty())
        {
            wait_ready(peer.connection, POLLOUT);
#if defined(MSG_NOSIGNAL)
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto size = send(peer.connection, bytes.data(), bytes.size(), flags);
            if (size > 0)
            {
                bytes.remove_prefix(static_cast<std::size_t>(size));
            }
            else if (size != -1 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK))
            {
                throw std::runtime_error("kernel sample peer send failed: " + client.log);
            }
        }
    };
    auto receive_exact = [&](const std::string& expected) {
        std::string received(expected.size(), '\0');
        std::size_t offset{};
        while (offset < received.size())
        {
            wait_ready(peer.connection, POLLIN);
            const auto size = recv(peer.connection, received.data() + offset, received.size() - offset, 0);
            if (size > 0)
            {
                offset += static_cast<std::size_t>(size);
            }
            else if (size != -1 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK))
            {
                throw std::runtime_error("kernel sample peer receive failed: " + client.log);
            }
        }
        if (received != expected)
        {
            throw std::runtime_error("kernel sample peer frame mismatch: " + client.log);
        }
    };
    using connection_load::frame_kind;
    const connection_load::identity owner{97, 3, 1};
    auto wire = [&](frame_kind kind, std::uint64_t sequence = 0) {
        const auto body = connection_load::make_frame(kind, owner, sequence, kind == frame_kind::data ? 1 : 0);
        return std::string(1, static_cast<char>(body.size())) + body;
    };
    receive_exact(wire(frame_kind::hello, 1));
    send_bytes(wire(frame_kind::hello_ack));
    receive_exact(wire(frame_kind::start));
    send_bytes(wire(frame_kind::start_ack));
    receive_exact(wire(frame_kind::data, 1));
    send_bytes(wire(frame_kind::data, 1));
    receive_exact(wire(frame_kind::data, 2));
    // Deliberately withhold the second Echo, but keep the accepted socket open
    // through the child's real request timeout and cancellation/drain.
    const auto status = client.wait();
    const auto output = read_file(client.log);
    const auto history_position = output.find("\"event\":\"tcp_kernel_sample_history\"");
    const auto summary_position = output.find("\"event\":\"operation_trace_summary\"");
    const auto snapshot_position = output.find("\"event\":\"connection_diagnostic\"");
    const auto result_position = output.find("\"event\":\"result\"");
    if (status != 1 || history_position == std::string::npos || summary_position == std::string::npos ||
        snapshot_position == std::string::npos || result_position == std::string::npos ||
        !(history_position < summary_position && summary_position < snapshot_position &&
            snapshot_position < result_position))
    {
        throw std::runtime_error("periodic kernel sample output missing: " + client.log);
    }
    const auto history = output.substr(history_position, output.find('\n', history_position) - history_position);
    const auto summary = output.substr(summary_position, output.find('\n', summary_position) - summary_position);
    const auto result = output.substr(result_position);
    if (history.find("\"schema\":2,") == std::string::npos ||
        history.find("\"clock_domain\":\"local_steady_clock\"") == std::string::npos ||
        history.find("\"clock_origin_us\":") == std::string::npos ||
        history.find("\"clock_origin_unit\":\"microseconds_since_steady_clock_epoch\"") == std::string::npos ||
        history.find("\"retention_policy\":\"first_4_latest_4\"") == std::string::npos ||
        counter(history, "head_sample_capacity") != 4 || counter(history, "tail_sample_capacity") != 4 ||
        counter(history, "sample_ordinal") != 1 ||
        history.find("\"sample_phase\":\"periodic_io_worker\"") == std::string::npos ||
        history.find("\"run_id\":97,\"client_id\":3,\"connection_id\":1") == std::string::npos ||
        counter(history, "sample_time_us") < 1000000 || counter(history, "no_completion_age_us") < 1000000 ||
        counter(history, "next_receive") != 2 || counter(history, "next_send") != 3 ||
        counter(summary, "worker_markers_completed") < 1 || counter(summary, "tcp_kernel_histories_emitted") != 1 ||
        counter(summary, "tcp_kernel_histories_omitted") != 0 || counter(summary, "sampler_calls") < 1 ||
        summary.find("\"output_truncated\":false") == std::string::npos ||
        summary.find("\"operation_scope\":\"composed_only\",\"low_level_operations\":false") == std::string::npos ||
        result.find("\"cleanup_complete\":true") == std::string::npos ||
        result.find("\"success\":false") == std::string::npos || result.find("request timeout:") == std::string::npos ||
        result.find("\"operation_trace_enabled\":true,\"operation_trace_emitted\":true") == std::string::npos ||
        result.find("\"tcp_kernel_samples_enabled\":true") == std::string::npos ||
        output.find("socket_operation_trace_enabled") != std::string::npos || counter(result, "data_received") != 1 ||
        counter(result, "finished") != 0)
    {
        throw std::runtime_error("periodic kernel sample scope/drain evidence missing: " + client.log);
    }
#if defined(__APPLE__)
    if (history.find("\"tcp_info\":{\"state\":4,") == std::string::npos)
    {
        throw std::runtime_error("periodic established Apple TCP info missing: " + client.log);
    }
#else
    if (history.find("\"tcp_info\":null,\"unavailable_reason\":\"unsupported_platform\"") == std::string::npos)
    {
        throw std::runtime_error("unsupported periodic TCP info reason missing: " + client.log);
    }
#endif
    std::cout << "controlled post-DATA stall retained periodic samples before timeout cleanup: " << client.log << '\n';
}

void fragmented_window(const std::string& executable) {
    using connection_load::frame_kind;
    const connection_load::identity owner{83, 1, 1};
    child server(executable,
        {"--role", "server", "--listen", "127.0.0.1", "--port", "0", "--run-id", "83", "--scenario", "traffic",
            "--payload", "1024", "--window", "4", "--max-connections", "1", "--expected-connections", "1",
            "--hold-seconds", "0.01", "--deadline-seconds", "5", "--cleanup-seconds", "1"});
    const auto port = std::stoi(server.port());

    struct raw_peer
    {
        int fd = socket(AF_INET, SOCK_STREAM, 0);

        ~raw_peer() {
            if (fd != -1)
            {
                close(fd);
            }
        }
    } peer;

    if (peer.fd == -1 || fcntl(peer.fd, F_SETFD, FD_CLOEXEC) == -1)
    {
        throw std::runtime_error("fragmented peer socket setup failed");
    }
#if defined(SO_NOSIGPIPE)
    const int no_sigpipe = 1;
    if (setsockopt(peer.fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof(no_sigpipe)))
    {
        throw std::runtime_error("fragmented peer SIGPIPE setup failed");
    }
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    if (connect(peer.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ||
        fcntl(peer.fd, F_SETFL, O_NONBLOCK) == -1)
    {
        throw std::runtime_error("fragmented peer connect failed");
    }
    const auto deadline = clock_type::now() + std::chrono::seconds(4);
    auto wait_ready = [&](short events) {
        while (clock_type::now() < deadline)
        {
            pollfd descriptor{peer.fd, events, 0};
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock_type::now());
            const auto result = ::poll(&descriptor, 1, static_cast<int>(remaining.count()) + 1);
            if (result > 0)
            {
                return;
            }
            if (result < 0 && errno != EINTR)
            {
                throw std::runtime_error("fragmented peer poll failed");
            }
        }
        throw std::runtime_error("fragmented peer IO deadline exceeded: " + server.log);
    };
    auto send_bytes = [&](std::string_view bytes) {
        while (!bytes.empty())
        {
            wait_ready(POLLOUT);
#if defined(MSG_NOSIGNAL)
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto written = send(peer.fd, bytes.data(), bytes.size(), flags);
            if (written > 0)
            {
                bytes.remove_prefix(static_cast<std::size_t>(written));
            }
            else if (written == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            {
                continue;
            }
            else
            {
                throw std::runtime_error("fragmented peer send failed: " + server.log);
            }
        }
    };
    auto receive_exact = [&](const std::string& expected) {
        std::string received(expected.size(), '\0');
        std::size_t offset{};
        while (offset < received.size())
        {
            wait_ready(POLLIN);
            const auto size = recv(peer.fd, received.data() + offset, received.size() - offset, 0);
            if (size > 0)
            {
                offset += static_cast<std::size_t>(size);
            }
            else if (size == -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK))
            {
                continue;
            }
            else
            {
                throw std::runtime_error("fragmented peer receive failed: " + server.log);
            }
        }
        if (received != expected)
        {
            throw std::runtime_error("fragmented peer response content/order mismatch: " + server.log);
        }
    };
    auto wire = [&](frame_kind kind, std::uint64_t sequence = 0) {
        const auto body = connection_load::make_frame(kind, owner, sequence, kind == frame_kind::data ? 1024 : 0);
        std::string prefix;
        if (body.size() < 254)
        {
            prefix += static_cast<char>(body.size());
        }
        else
        {
            prefix += '\xfe';
            prefix += static_cast<char>(body.size());
            prefix += static_cast<char>(body.size() >> 8);
        }
        return prefix + body;
    };
    auto require_incomplete = [&] {
        // An intentionally incomplete frame must not produce an Echo. Polling
        // only gates this withheld-byte phase; elapsed time is not a throughput assertion.
        pollfd descriptor{peer.fd, POLLIN, 0};
        const auto result = ::poll(&descriptor, 1, 100);
        if (result != 0)
        {
            throw std::runtime_error("incomplete frame unexpectedly produced socket input: " + server.log);
        }
    };
    send_bytes(wire(frame_kind::hello, 1));
    receive_exact(wire(frame_kind::hello_ack));
    send_bytes(wire(frame_kind::start));
    receive_exact(wire(frame_kind::start_ack));
    std::string first_window;
    for (unsigned sequence = 1; sequence <= 4; ++sequence)
    {
        first_window += wire(frame_kind::data, sequence);
    }
    if (first_window.size() != 4364)
    {
        throw std::runtime_error("fragmented window no longer covers the 4096-byte receive cap");
    }
    send_bytes(std::string_view(first_window).substr(0, 1)); // Partial three-byte prefix.
    require_incomplete();
    send_bytes(std::string_view(first_window).substr(1, 1089)); // One body byte still withheld.
    require_incomplete();
    send_bytes(std::string_view(first_window).substr(1090)); // Complete frame plus three queued frames.
    receive_exact(first_window);
    std::string second_window;
    for (unsigned sequence = 5; sequence <= 8; ++sequence)
    {
        second_window += wire(frame_kind::data, sequence);
    }
    send_bytes(second_window); // One application write larger than the receive cap.
    receive_exact(second_window);
    send_bytes(wire(frame_kind::finish, 8));
    receive_exact(wire(frame_kind::finish_ack, 8));
    shutdown(peer.fd, SHUT_RDWR);
    close(peer.fd);
    peer.fd = -1;
    const auto status = server.wait();
    const auto output = read_file(server.log);
    if (status || output.find("\"finished\":1") == std::string::npos ||
        output.find("\"data_sent\":8,") == std::string::npos ||
        output.find("\"data_received\":8,") == std::string::npos)
    {
        throw std::runtime_error("fragmented window did not finish cleanly: " + server.log + "\n" + output);
    }
    std::cout << "fragmented prefix/body and two 4364-byte windows recovered 8 exact Echo frames: " << server.log
              << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try
    {
        if (argc != 3)
        {
            throw std::runtime_error("usage: native_control_test NATIVE_EXECUTABLE ARKNET_EXECUTABLE");
        }
        char path[] = "/tmp/arknet-native-process-test-XXXXXX";
        const auto result = mkdtemp(path);
        if (!result)
        {
            throw std::runtime_error("mkdtemp failed");
        }
        directory = result;
        std::cout << "child logs and exits: " << directory << '\n';
        invalid_arguments(argv[1]);
        silent_peer(argv[1]);
        silent_peer(argv[1], true);
        progress_then_stall(argv[1]);
        pair(argv[1], argv[1], 1, "idle", 1024, false, true);
        fragmented_window(argv[1]);
        fragmented_window(argv[2]);
        pair(argv[1], argv[1], 1, "idle");
        pair(argv[1], argv[1], 3, "idle", 1024, true);
        pair(argv[2], argv[2], 3, "idle", 1024, true);
        for (const auto count : {1u, 8u, 32u})
        {
            pair(argv[1], argv[1], count);
        }
        pair(argv[1], argv[1], 1, "traffic", 1);
        pair(argv[1], argv[1], 1, "traffic", 4023);
        pair(argv[1], argv[2], 8);
        pair(argv[2], argv[1], 8);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
