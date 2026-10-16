// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 2)
    {
        std::cerr << "usage: connection_load_backpressure BENCHMARK_EXECUTABLE\n";
        return 1;
    }
    int descriptors[2];
    if (pipe(descriptors) || fcntl(descriptors[1], F_SETFL, O_NONBLOCK) == -1)
    {
        std::cerr << "could not prepare test-owned output pipe\n";
        return 1;
    }
    const std::array<char, 4096> bytes{};
    while (write(descriptors[1], bytes.data(), bytes.size()) >= 0)
    {
    }
    if (errno != EAGAIN || fcntl(descriptors[1], F_SETFL, 0) == -1)
    {
        std::cerr << "could not fill test-owned output pipe\n";
        return 1;
    }
    const auto child = fork();
    if (child == -1)
    {
        std::cerr << "fork failed\n";
        return 1;
    }
    if (!child)
    {
        const auto errors = open("/dev/null", O_WRONLY);
        if (errors == -1 || dup2(descriptors[1], STDOUT_FILENO) == -1 || dup2(errors, STDERR_FILENO) == -1)
        {
            std::_Exit(127);
        }
        close(descriptors[0]);
        close(descriptors[1]);
        close(errors);
        execl(argv[1], argv[1], "--role", "server", "--listen", "127.0.0.1", "--port", "0", "--run-id", "1",
            "--deadline-seconds", "0.1", "--cleanup-seconds", "1", static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    close(descriptors[1]);
    // Keep the reader open without consuming data. The child's first stdout
    // flush blocks; its watchdog must not depend on flushing any tied stream.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int status{};
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto result = waitpid(child, &status, WNOHANG);
        if (result == child)
        {
            close(descriptors[0]);
            if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
            {
                std::cout << "watchdog terminated with SIGKILL despite an unread full stdout pipe\n";
                return 0;
            }
            std::cerr << "unexpected child status " << status << '\n';
            return 1;
        }
        if (result == -1 && errno != EINTR)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) == -1 && errno == EINTR)
    {
    }
    close(descriptors[0]);
    std::cerr << "watchdog failed to terminate under stdout backpressure; child was reaped\n";
    return 1;
}
