// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include "runtime.hpp"
#include "resources.hpp"
#include "work.hpp"
#include <arknet/base/error.hpp>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

namespace
{
using clock_type = std::chrono::steady_clock;
using socket_type = asio::ip::tcp::socket;
using namespace std::chrono_literals;
struct options
{
    std::string model = "shared";
    std::size_t threads = 1, clients = 16, window = 1, payload = 1024, work = 0;
    double seconds = 3, warmup = 1;
};

std::size_t number(std::string_view value)
{
    std::size_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("invalid integer");
    return result;
}

double duration(const std::string& value)
{
    std::size_t count{};
    const double result = std::stod(value, &count);
    if (count != value.size() || !std::isfinite(result)) throw std::invalid_argument("invalid duration");
    return result;
}

options parse(int argc, char** argv)
{
    options result;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        if (key == "--help")
        {
            std::cout << "arknet_coroutine_benchmark --protocol tcp --io-model shared|sharded "
                "--io-threads COUNT --clients COUNT --window COUNT --payload BYTES "
                "--seconds SECONDS --warmup SECONDS --work ITERATIONS\n";
            std::exit(0);
        }
        if (++i == argc) throw std::invalid_argument("missing option value");
        const std::string value = argv[i];
        if (key == "--io-model") result.model = value;
        else if (key == "--io-threads") result.threads = number(value);
        else if (key == "--clients") result.clients = number(value);
        else if (key == "--window") result.window = number(value);
        else if (key == "--payload") result.payload = number(value);
        else if (key == "--work") result.work = number(value);
        else if (key == "--seconds") result.seconds = duration(value);
        else if (key == "--warmup") result.warmup = duration(value);
        else if (key == "--protocol" && value == "tcp") {}
        else if (key == "--certs") {}
        else throw std::invalid_argument("unsupported option");
    }
    if ((result.model != "shared" && result.model != "sharded") || result.threads < 1 || result.threads > 64 ||
        result.clients < 1 || result.clients > 64 || result.window < 1 || result.window > 64 ||
        result.payload < 16 || result.payload > 65507 || result.work > 10000000 ||
        result.seconds <= 0 || result.seconds > 600 || result.warmup < 0 || result.warmup > 600 ||
        result.clients * result.window * result.payload > 256 * 1024 * 1024)
        throw std::invalid_argument("option out of range");
    return result;
}

struct phase_result
{
    std::uint64_t messages{}, observations{};
    double elapsed{};
    std::vector<double> samples;
};
struct client_result { phase_result warmup, measured; };
struct shared_state
{
    std::atomic<std::size_t> ready{}, errors{};
    std::atomic<bool> begin{}, stopping{};
    clock_type::time_point started;
};

asio::awaitable<void> serve(std::shared_ptr<socket_type> socket, options settings, shared_state& state)
{
    socket->set_option(asio::ip::tcp::no_delay(true));
    std::string data(settings.payload, '\0');
    try
    {
        for (;;)
        {
            co_await asio::async_read(*socket, asio::buffer(data), asio::use_awaitable);
            benchmark_work(settings.work, data);
            co_await asio::async_write(*socket, asio::buffer(data), asio::use_awaitable);
        }
    }
    catch (const arknet::system_error& error)
    {
        if (!state.stopping && error.code() != asio::error::eof) ++state.errors;
    }
}

asio::awaitable<void> accept(benchmark_runtime& runtime, asio::ip::tcp::acceptor& listener,
    options settings, std::vector<std::future<void>>& sessions,
    std::vector<std::shared_ptr<socket_type>>& sockets, shared_state& state)
{
    try
    {
        for (std::size_t i = 0;; ++i)
        {
            auto socket = std::make_shared<socket_type>(asio::make_strand(runtime.context_at(i)));
            co_await listener.async_accept(*socket, asio::use_awaitable);
            sockets.push_back(socket);
            sessions.push_back(asio::co_spawn(socket->get_executor(), serve(socket, settings, state), asio::use_future));
        }
    }
    catch (const arknet::system_error&)
    {
        if (!state.stopping) ++state.errors;
    }
}

asio::awaitable<client_result> client(std::shared_ptr<socket_type> connection, asio::ip::tcp::endpoint address,
    options settings, std::size_t id, shared_state& state)
{
    auto& socket = *connection;
    const auto executor = socket.get_executor();
    try { co_await socket.async_connect(address, asio::use_awaitable); }
    catch (...) { ++state.errors; ++state.ready; throw; }
    socket.set_option(asio::ip::tcp::no_delay(true));
    ++state.ready;
    asio::steady_timer gate(executor);
    while (!state.begin && !state.stopping)
    {
        gate.expires_after(1ms);
        co_await gate.async_wait(asio::use_awaitable);
    }
    if (state.stopping) co_return client_result{};
    client_result result;
    std::string sent(settings.payload * settings.window, '\0'), received(sent.size(), '\0');
    std::uint64_t sequence{};
    std::mt19937_64 random(id);
    for (unsigned phase = 0; phase != 2; ++phase)
    {
        auto& stats = phase == 0 ? result.warmup : result.measured;
        const auto phase_start = state.started + std::chrono::duration_cast<clock_type::duration>(
            std::chrono::duration<double>(phase == 0 ? 0 : settings.warmup));
        const auto deadline = phase_start + std::chrono::duration_cast<clock_type::duration>(
            std::chrono::duration<double>(phase == 0 ? settings.warmup : settings.seconds));
        while (clock_type::now() < deadline)
        {
            for (std::size_t item = 0; item < settings.window; ++item, ++sequence)
                for (std::size_t byte = 0; byte < settings.payload; ++byte)
                    sent[item * settings.payload + byte] = static_cast<char>((sequence * 31 + id * 17 + byte) & 255);
            const auto began = clock_type::now();
            co_await asio::async_write(socket, asio::buffer(sent), asio::use_awaitable);
            co_await asio::async_read(socket, asio::buffer(received), asio::use_awaitable);
            if (sent != received) ++state.errors;
            stats.messages += settings.window;
            const auto latency = std::chrono::duration<double, std::micro>(clock_type::now() - began).count();
            ++stats.observations;
            if (stats.samples.size() < 8192) stats.samples.push_back(latency);
            else
            {
                const auto index = std::uniform_int_distribution<std::uint64_t>(0, stats.observations - 1)(random);
                if (index < stats.samples.size()) stats.samples[index] = latency;
            }
        }
        stats.elapsed = std::chrono::duration<double>(clock_type::now() - phase_start).count();
    }
    co_return result;
}

void run(const options& settings, void (*after_connect)() = nullptr)
{
    benchmark_runtime runtime(settings.model, settings.threads);
    shared_state state;
    asio::ip::tcp::acceptor listener(asio::make_strand(runtime.context_at()),
        {asio::ip::make_address("127.0.0.1"), 0});
    std::vector<std::future<void>> sessions;
    std::vector<std::shared_ptr<socket_type>> sockets;
    std::vector<std::shared_ptr<socket_type>> client_sockets;
    std::vector<std::future<client_result>> clients;
    std::future<void> accepting;
    struct runtime_cleanup
    {
        benchmark_runtime& runtime;
        shared_state& state;
        asio::ip::tcp::acceptor& listener;
        std::future<void>& accepting;
        std::vector<std::shared_ptr<socket_type>>& sockets;
        std::vector<std::shared_ptr<socket_type>>& clients;
        ~runtime_cleanup()
        {
            state.stopping = true;
            for (auto& socket : clients)
                asio::post(socket->get_executor(), [socket] { arknet::error_code ec; socket->close(ec); });
            asio::post(listener.get_executor(), [&] { arknet::error_code ec; listener.close(ec); });
            // Accept is the sole writer of sockets; wait before reading its output.
            if (accepting.valid()) accepting.wait();
            for (auto& socket : sockets)
                asio::post(socket->get_executor(), [socket] { arknet::error_code ec; socket->close(ec); });
            runtime.stop();
        }
    } cleanup{runtime, state, listener, accepting, sockets, client_sockets};
    accepting = asio::co_spawn(listener.get_executor(), accept(runtime, listener, settings, sessions, sockets, state),
        asio::use_future);
    for (std::size_t i = 0; i < settings.clients; ++i)
    {
        auto socket = std::make_shared<socket_type>(asio::make_strand(runtime.context_at(i)));
        client_sockets.push_back(socket);
        clients.push_back(asio::co_spawn(socket->get_executor(),
            client(socket, listener.local_endpoint(), settings, i, state), asio::use_future));
    }
    const auto connection_deadline = clock_type::now() + 10s;
    while (state.ready < settings.clients && clock_type::now() < connection_deadline)
        std::this_thread::sleep_for(1ms);
    if (state.ready != settings.clients || state.errors) throw std::runtime_error("coroutine connect failed");
    if (after_connect) after_connect();
    const auto cpu_start = process_cpu();
    state.started = clock_type::now();
    state.begin = true;
    std::uint64_t messages{}, observations{}, warmup_messages{};
    double elapsed{};
    std::vector<double> samples;
    for (auto& future : clients)
    {
        if (future.wait_for(std::chrono::duration<double>(settings.seconds + settings.warmup + 15)) != std::future_status::ready)
            throw std::runtime_error("coroutine traffic timed out");
        auto result = future.get();
        messages += result.measured.messages;
        observations += result.measured.observations;
        warmup_messages += result.warmup.messages;
        elapsed = (std::max)(elapsed, result.measured.elapsed);
        samples.insert(samples.end(), result.measured.samples.begin(), result.measured.samples.end());
    }
    const auto cpu_end = process_cpu();
    const auto rss = peak_rss();
    state.stopping = true;
    asio::post(listener.get_executor(), [&] { listener.close(); });
    if (accepting.wait_for(5s) != std::future_status::ready) throw std::runtime_error("coroutine accept stop timed out");
    accepting.get();
    for (auto& socket : sockets)
        asio::post(socket->get_executor(), [socket] { arknet::error_code ec; socket->close(ec); });
    for (auto& future : sessions)
    {
        if (future.wait_for(5s) != std::future_status::ready) throw std::runtime_error("coroutine session stop timed out");
        future.get();
    }
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double fraction)
    {
        return samples.empty() ? 0 : samples[static_cast<std::size_t>((samples.size() - 1) * fraction)];
    };
    const bool ok = !state.errors && messages > 0 && (settings.warmup == 0 || warmup_messages > 0);
    std::cout << std::setprecision(12) << "{\"schema_version\":1,\"implementation\":\"asio-coroutine\",\"backend\":\""
#ifdef ARKNET_USE_BOOST_ASIO
        << "boost"
#else
        << "standalone"
#endif
        << "\",\"protocol\":\"tcp\",\"payload_bytes\":" << settings.payload
        << ",\"clients\":" << settings.clients << ",\"window\":" << settings.window
        << ",\"io_model\":\"" << settings.model << "\",\"io_threads\":" << settings.threads
        << ",\"io_contexts\":" << (settings.model == "shared" ? 1 : settings.threads)
        << ",\"requested_seconds\":" << settings.seconds << ",\"warmup_seconds\":" << settings.warmup
        << ",\"handler_work\":" << settings.work << ",\"elapsed_seconds\":" << elapsed
        << ",\"messages\":" << messages << ",\"roundtrips_per_second\":" << messages / elapsed
        << ",\"payload_mib_per_second\":" << messages * settings.payload * 2 / elapsed / 1048576
        << ",\"latency_mode\":\"" << (settings.window == 1 ? "message" : "batch")
        << "\",\"rtt_samples\":" << samples.size() << ",\"rtt_observations\":" << observations
        << ",\"rtt_p50_us\":" << percentile(0.5) << ",\"rtt_p95_us\":" << percentile(0.95)
        << ",\"rtt_p99_us\":" << percentile(0.99) << ",\"warmup_messages\":" << warmup_messages
        << ",\"total_cpu_seconds\":" << (cpu_start && cpu_end ? std::to_string(*cpu_end - *cpu_start) : "null")
        << ",\"peak_rss_bytes\":" << (rss ? std::to_string(*rss) : "null")
        << ",\"content_or_io_errors\":" << state.errors
        << ",\"ok\":" << (ok ? "true" : "false") << "}\n";
    if (!ok) throw std::runtime_error("coroutine traffic validation failed");
}
}

#ifndef ARKNET_COROUTINE_BENCHMARK_NO_MAIN
int main(int argc, char** argv)
{
    try { run(parse(argc, argv)); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}
#endif
