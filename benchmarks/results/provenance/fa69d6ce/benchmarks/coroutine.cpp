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
    std::string model = "shared", execution = "coroutine";
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
                "--execution callback|coroutine "
                "--io-threads COUNT --clients COUNT --window COUNT --payload BYTES "
                "--seconds SECONDS --warmup SECONDS --work ITERATIONS\n";
            std::exit(0);
        }
        if (++i == argc) throw std::invalid_argument("missing option value");
        const std::string value = argv[i];
        if (key == "--io-model") result.model = value;
        else if (key == "--execution") result.execution = value;
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
    if ((result.model != "shared" && result.model != "sharded") ||
        (result.execution != "callback" && result.execution != "coroutine") ||
        result.threads < 1 || result.threads > 64 ||
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

struct client_traffic
{
    client_traffic(options settings, std::size_t id, shared_state& state)
        : settings(std::move(settings)), id(id), state(state),
          sent(this->settings.payload * this->settings.window, '\0'), received(sent.size(), '\0'), random(id) {}
    options settings;
    std::size_t id;
    shared_state& state;
    std::string sent, received;
    client_result result;
    std::mt19937_64 random;
    std::uint64_t sequence{};
    unsigned phase{};
    clock_type::time_point began;

    bool next()
    {
        if (state.stopping) return false;
        while (phase != 2)
        {
            const auto start = state.started + std::chrono::duration_cast<clock_type::duration>(
                std::chrono::duration<double>(phase == 0 ? 0 : settings.warmup));
            const auto deadline = start + std::chrono::duration_cast<clock_type::duration>(
                std::chrono::duration<double>(phase == 0 ? settings.warmup : settings.seconds));
            if (clock_type::now() < deadline) break;
            (phase == 0 ? result.warmup : result.measured).elapsed =
                std::chrono::duration<double>(clock_type::now() - start).count();
            ++phase;
        }
        if (phase == 2) return false;
        for (std::size_t item = 0; item < settings.window; ++item, ++sequence)
            for (std::size_t byte = 0; byte < settings.payload; ++byte)
                sent[item * settings.payload + byte] = static_cast<char>((sequence * 31 + id * 17 + byte) & 255);
        began = clock_type::now();
        return true;
    }

    void received_batch()
    {
        if (sent != received) ++state.errors;
        auto& stats = phase == 0 ? result.warmup : result.measured;
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
};

void process_batch(std::size_t work, std::size_t payload, std::string_view data)
{
    for (std::size_t offset = 0; offset < data.size(); offset += payload)
        benchmark_work(work, data.substr(offset, payload));
}

struct callback_session : std::enable_shared_from_this<callback_session>
{
    callback_session(std::shared_ptr<socket_type> socket, const options& settings, shared_state& state)
        : socket(std::move(socket)), state(state), work(settings.work), payload(settings.payload),
          data(settings.payload * settings.window, '\0') {}
    std::shared_ptr<socket_type> socket;
    shared_state& state;
    std::size_t work, payload;
    std::string data;
    std::promise<void> completed;

    void finish(arknet::error_code error)
    {
        if (!state.stopping && error != asio::error::eof) ++state.errors;
        completed.set_value();
    }
    void start()
    {
        arknet::error_code error;
        socket->set_option(asio::ip::tcp::no_delay(true), error);
        if (error) finish(error);
        else read();
    }
    void read()
    {
        asio::async_read(*socket, asio::buffer(data),
            [self = shared_from_this()](arknet::error_code error, std::size_t)
        {
            if (error) { self->finish(error); return; }
            process_batch(self->work, self->payload, self->data);
            asio::async_write(*self->socket, asio::buffer(self->data),
                [self](arknet::error_code error, std::size_t)
            {
                if (error) self->finish(error);
                else self->read();
            });
        });
    }
};

struct callback_acceptor : std::enable_shared_from_this<callback_acceptor>
{
    callback_acceptor(benchmark_runtime& runtime, asio::ip::tcp::acceptor& listener, const options& settings,
        std::vector<std::future<void>>& sessions, std::vector<std::shared_ptr<socket_type>>& sockets,
        shared_state& state)
        : runtime(runtime), listener(listener), settings(settings), sessions(sessions), sockets(sockets), state(state) {}
    benchmark_runtime& runtime;
    asio::ip::tcp::acceptor& listener;
    options settings;
    std::vector<std::future<void>>& sessions;
    std::vector<std::shared_ptr<socket_type>>& sockets;
    shared_state& state;
    std::promise<void> completed;

    void next(std::size_t index = 0)
    {
        auto socket = std::make_shared<socket_type>(asio::make_strand(runtime.context_at(index)));
        listener.async_accept(*socket, [self = shared_from_this(), socket, index](arknet::error_code error)
        {
            if (error)
            {
                if (!self->state.stopping) ++self->state.errors;
                self->completed.set_value();
                return;
            }
            self->sockets.push_back(socket);
            auto session = std::make_shared<callback_session>(socket, self->settings, self->state);
            self->sessions.push_back(session->completed.get_future());
            asio::post(socket->get_executor(), [session] { session->start(); });
            self->next(index + 1);
        });
    }
};

struct callback_client : std::enable_shared_from_this<callback_client>
{
    callback_client(std::shared_ptr<socket_type> socket, const options& settings, std::size_t id, shared_state& state)
        : socket(std::move(socket)), traffic(settings, id, state), gate(this->socket->get_executor()) {}
    std::shared_ptr<socket_type> socket;
    client_traffic traffic;
    asio::steady_timer gate;
    std::promise<client_result> completed;

    void fail(arknet::error_code error)
    {
        if (!traffic.state.stopping) ++traffic.state.errors;
        completed.set_exception(std::make_exception_ptr(arknet::system_error(error)));
    }
    void start(asio::ip::tcp::endpoint endpoint)
    {
        socket->async_connect(endpoint, [self = shared_from_this()](arknet::error_code error)
        {
            if (!error) self->socket->set_option(asio::ip::tcp::no_delay(true), error);
            ++self->traffic.state.ready;
            if (error) self->fail(error);
            else self->wait();
        });
    }
    void wait()
    {
        if (traffic.state.stopping) { completed.set_value(client_result{}); return; }
        if (traffic.state.begin) { next(); return; }
        gate.expires_after(1ms);
        gate.async_wait([self = shared_from_this()](arknet::error_code error)
        {
            if (error) self->fail(error);
            else self->wait();
        });
    }
    void next()
    {
        if (!traffic.next()) { completed.set_value(std::move(traffic.result)); return; }
        asio::async_write(*socket, asio::buffer(traffic.sent),
            [self = shared_from_this()](arknet::error_code error, std::size_t)
        {
            if (error) { self->fail(error); return; }
            asio::async_read(*self->socket, asio::buffer(self->traffic.received),
                [self](arknet::error_code error, std::size_t)
            {
                if (error) { self->fail(error); return; }
                self->traffic.received_batch();
                self->next();
            });
        });
    }
};

asio::awaitable<void> serve(std::shared_ptr<socket_type> socket, options settings, shared_state& state)
{
    socket->set_option(asio::ip::tcp::no_delay(true));
    // Read complete batches before echoing so opposing writes cannot stall each other.
    std::string data(settings.payload * settings.window, '\0');
    try
    {
        for (;;)
        {
            co_await asio::async_read(*socket, asio::buffer(data), asio::use_awaitable);
            process_batch(settings.work, settings.payload, data);
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
    client_traffic traffic(settings, id, state);
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
    while (traffic.next())
    {
        co_await asio::async_write(socket, asio::buffer(traffic.sent), asio::use_awaitable);
        co_await asio::async_read(socket, asio::buffer(traffic.received), asio::use_awaitable);
        traffic.received_batch();
    }
    co_return std::move(traffic.result);
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
    if (settings.execution == "coroutine")
        accepting = asio::co_spawn(listener.get_executor(), accept(runtime, listener, settings, sessions, sockets, state),
            asio::use_future);
    else
    {
        auto acceptor = std::make_shared<callback_acceptor>(runtime, listener, settings, sessions, sockets, state);
        accepting = acceptor->completed.get_future();
        asio::post(listener.get_executor(), [acceptor] { acceptor->next(); });
    }
    for (std::size_t i = 0; i < settings.clients; ++i)
    {
        auto socket = std::make_shared<socket_type>(asio::make_strand(runtime.context_at(i)));
        client_sockets.push_back(socket);
        if (settings.execution == "coroutine")
            clients.push_back(asio::co_spawn(socket->get_executor(),
                client(socket, listener.local_endpoint(), settings, i, state), asio::use_future));
        else
        {
            auto peer = std::make_shared<callback_client>(socket, settings, i, state);
            clients.push_back(peer->completed.get_future());
            asio::post(socket->get_executor(), [peer, endpoint = listener.local_endpoint()] { peer->start(endpoint); });
        }
    }
    const auto connection_deadline = clock_type::now() + 10s;
    while (state.ready < settings.clients && clock_type::now() < connection_deadline)
        std::this_thread::sleep_for(1ms);
    if (state.ready != settings.clients || state.errors) throw std::runtime_error("native TCP connect failed");
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
            throw std::runtime_error("native TCP traffic timed out");
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
    if (accepting.wait_for(5s) != std::future_status::ready) throw std::runtime_error("native TCP accept stop timed out");
    accepting.get();
    for (auto& socket : sockets)
        asio::post(socket->get_executor(), [socket] { arknet::error_code ec; socket->close(ec); });
    for (auto& future : sessions)
    {
        if (future.wait_for(5s) != std::future_status::ready) throw std::runtime_error("native TCP session stop timed out");
        future.get();
    }
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double fraction)
    {
        return samples.empty() ? 0 : samples[static_cast<std::size_t>((samples.size() - 1) * fraction)];
    };
    const bool ok = !state.errors && messages > 0 && (settings.warmup == 0 || warmup_messages > 0);
    std::cout << std::setprecision(12) << "{\"schema_version\":1,\"implementation\":\"asio-" << settings.execution
        << "\",\"execution\":\"" << settings.execution << "\",\"backend\":\""
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
    if (!ok) throw std::runtime_error("native TCP traffic validation failed");
}
}

#ifndef ARKNET_COROUTINE_BENCHMARK_NO_MAIN
int main(int argc, char** argv)
{
    try { run(parse(argc, argv)); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}
#endif
