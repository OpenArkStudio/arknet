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
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

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
    const std::string& scenario = "traffic", unsigned payload = 1024, bool phases = false) {
    const std::vector<std::string> common{"--run-id", "71", "--scenario", scenario, "--payload",
        std::to_string(payload), "--window", "4", "--max-connections", std::to_string(count), "--hold-seconds",
        phases ? "3.2" : "0.03", "--deadline-seconds", phases ? "7" : "5", "--cleanup-seconds", "1"};
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
             {"--connections", "5000", "--window", "4", "--max-inflight", "19999"}})
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
