// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <arknet/redis/client.hpp>
#include "resources.hpp"

namespace {

using clock_type = std::chrono::steady_clock;

struct options
{
    std::string host = "127.0.0.1";
    std::string port = "6379";
    std::string username;
    std::string ca;
    std::size_t clients = 4;
    std::size_t pipeline = 16;
    std::size_t window = 1;
    std::size_t payload = 1024;
    std::size_t rounds = 3;
    double seconds = 1;
    double warmup = 0.25;
    bool tls = false;
};

std::size_t integer(std::string_view text) {
    std::size_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        throw std::invalid_argument("Invalid integer option");
    }
    return value;
}

double duration(std::string_view text) {
    double value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !std::isfinite(value))
    {
        throw std::invalid_argument("Invalid duration option");
    }
    return value;
}

options arguments(int argc, char** argv) {
    options result;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        if (key == "--help")
        {
            std::cout << "arknet_redis_benchmark --host HOST --port PORT --username USER --tls --ca FILE "
                         "--clients N --pipeline N --window N --payload BYTES --rounds N "
                         "--seconds SECONDS --warmup SECONDS\n"
                         "Password: ARKNET_REDIS_PASSWORD environment variable. Workload: binary ECHO.\n"
                         "Each connection owns one IO context and thread. Each round emits one JSON line.\n"
                         "Latency measures a complete pipeline batch, including client queueing.\n"
                         "CPU is client process CPU usage: one fully occupied core is 100%.\n"
                         "In-flight command payload plus per-command overhead is capped at 64 MiB per connection.\n";
            std::exit(0);
        }
        if (key == "--tls")
        {
            result.tls = true;
            continue;
        }
        if (++i == argc)
        {
            throw std::invalid_argument("Missing value for " + key);
        }
        const std::string value = argv[i];
        if (key == "--host")
        {
            result.host = value;
        }
        else if (key == "--port")
        {
            result.port = value;
        }
        else if (key == "--username")
        {
            result.username = value;
        }
        else if (key == "--ca")
        {
            result.ca = value;
        }
        else if (key == "--clients")
        {
            result.clients = integer(value);
        }
        else if (key == "--pipeline")
        {
            result.pipeline = integer(value);
        }
        else if (key == "--window")
        {
            result.window = integer(value);
        }
        else if (key == "--payload")
        {
            result.payload = integer(value);
        }
        else if (key == "--rounds")
        {
            result.rounds = integer(value);
        }
        else if (key == "--seconds")
        {
            result.seconds = duration(value);
        }
        else if (key == "--warmup")
        {
            result.warmup = duration(value);
        }
        else
        {
            throw std::invalid_argument("Unknown option: " + key);
        }
    }
    if (result.host.empty() || result.port.empty() || !result.clients || result.clients > 128 || !result.pipeline ||
        result.pipeline > 1024 || !result.window || result.window > 64 || result.payload > 1024 * 1024 ||
        !result.rounds || result.rounds > 100 || result.seconds <= 0 || result.seconds > 3600 || result.warmup < 0 ||
        result.warmup > 3600 || (!result.tls && !result.ca.empty()))
    {
        throw std::invalid_argument("Invalid benchmark dimensions; use --help");
    }
    constexpr std::size_t budget = 64 * 1024 * 1024;
    const auto bytes_per_command = result.payload + std::max<std::size_t>(64, sizeof(arknet::redis::value));
    if (result.pipeline > budget / bytes_per_command || result.window > budget / bytes_per_command / result.pipeline)
    {
        throw std::invalid_argument("In-flight data exceeds the 64 MiB per-connection budget");
    }
    return result;
}

struct phase
{
    clock_type::time_point stop;
    std::atomic<std::size_t> running;
    std::promise<void> finished;

    phase(double seconds, std::size_t chains)
        : stop(clock_type::now() +
               std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(seconds)))
        , running(chains) {}
};

template<class Client>
struct request_chain : std::enable_shared_from_this<request_chain<Client>>
{
    std::shared_ptr<Client> client;
    std::shared_ptr<phase> measurement;
    std::vector<arknet::redis::command> commands;
    std::string payload;
    std::vector<double> samples;
    std::size_t sample_capacity = 1;
    std::string failure_reason;
    std::mt19937_64 random;
    std::uint64_t successes = 0;
    std::uint64_t failures = 0;
    std::uint64_t batches = 0;

    void submit() {
        const auto began = clock_type::now();
        client->async_pipeline(commands, [self = this->shared_from_this(), began](arknet::redis::result result) {
            bool valid = bool(result) && result.replies.size() == self->commands.size();
            for (const auto& reply : result.replies)
            {
                valid = valid && reply.type == arknet::redis::value_type::bulk_string && reply.text == self->payload;
            }
            if (valid)
            {
                self->successes += self->commands.size();
                ++self->batches;
                const auto latency = std::chrono::duration<double, std::micro>(clock_type::now() - began).count();
                if (self->samples.size() < self->sample_capacity)
                {
                    self->samples.push_back(latency);
                }
                else
                {
                    const auto index = std::uniform_int_distribution<std::uint64_t>(0, self->batches - 1)(self->random);
                    if (index < self->samples.size())
                    {
                        self->samples[static_cast<std::size_t>(index)] = latency;
                    }
                }
            }
            else
            {
                self->failures += self->commands.size();
                if (self->failure_reason.empty())
                {
                    if (result.transport_error)
                    {
                        self->failure_reason = result.transport_error.message();
                    }
                    else if (result.error != arknet::redis::client_errc::none)
                    {
                        self->failure_reason = "client error " + std::to_string(static_cast<int>(result.error));
                    }
                    else
                    {
                        self->failure_reason = "ECHO reply verification failed";
                    }
                }
            }
            if (valid && clock_type::now() < self->measurement->stop)
            {
                self->submit();
            }
            else if (self->measurement->running.fetch_sub(1) == 1)
            {
                self->measurement->finished.set_value();
            }
        });
    }
};

template<class Client>
bool measure(const options& settings, const std::vector<std::shared_ptr<Client>>& clients, double seconds,
    std::size_t round, bool report, std::vector<std::string>& reports) {
    auto measurement = std::make_shared<phase>(seconds, clients.size() * settings.window);
    auto finished = measurement->finished.get_future();
    std::vector<std::shared_ptr<request_chain<Client>>> chains;
    std::string payload(settings.payload, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i)
    {
        payload[i] = static_cast<char>(i % 256);
    }
    for (const auto& client : clients)
    {
        for (std::size_t i = 0; i < settings.window; ++i)
        {
            auto chain = std::make_shared<request_chain<Client>>();
            chain->client = client;
            chain->measurement = measurement;
            chain->payload = payload;
            chain->commands.assign(settings.pipeline, arknet::redis::command{"ECHO", payload});
            chain->random.seed(chains.size() + 12345);
            chain->sample_capacity = std::max<std::size_t>(1, 65536 / (clients.size() * settings.window));
            chain->samples.reserve(chain->sample_capacity);
            chains.push_back(std::move(chain));
        }
    }
    // Account only for the load phase; connection setup and payload preparation precede it.
    const auto started = clock_type::now();
    const auto cpu_measured_start = process_cpu();
    measurement->stop =
        started + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(seconds));
    for (const auto& chain : chains)
    {
        chain->submit();
    }
    if (finished.wait_for(std::chrono::duration<double>(seconds + 10)) != std::future_status::ready)
    {
        for (const auto& client : clients)
        {
            client->close();
        }
        finished.wait_for(std::chrono::seconds(10));
        throw std::runtime_error("Benchmark requests did not drain within the deadline");
    }
    const auto elapsed = std::chrono::duration<double>(clock_type::now() - started).count();
    const auto cpu_end = process_cpu();
    std::uint64_t successes = 0;
    std::uint64_t failures = 0;

    struct latency_sample
    {
        double microseconds;
        double weight;
    };

    std::vector<latency_sample> samples;
    std::string failure_reason;
    for (const auto& chain : chains)
    {
        successes += chain->successes;
        failures += chain->failures;
        if (!chain->samples.empty())
        {
            const auto weight = static_cast<double>(chain->batches) / chain->samples.size();
            for (const auto latency : chain->samples)
            {
                samples.push_back({latency, weight});
            }
        }
        if (failure_reason.empty())
        {
            failure_reason = chain->failure_reason;
        }
    }
    std::sort(samples.begin(), samples.end(),
        [](const auto& left, const auto& right) { return left.microseconds < right.microseconds; });
    const auto percentile = [&](double fraction) {
        long double total = 0;
        for (const auto& sample : samples)
        {
            total += sample.weight;
        }
        const auto target = total * fraction;
        long double cumulative = 0;
        for (const auto& sample : samples)
        {
            cumulative += sample.weight;
            if (cumulative >= target)
            {
                return sample.microseconds;
            }
        }
        return samples.empty() ? 0.0 : samples.back().microseconds;
    };
    const bool passed = failures == 0 && successes != 0;
    if (report && passed)
    {
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(12)
               << "{\"schema_version\":1,\"protocol\":\"redis\",\"workload\":\"binary_echo\",\"api\":\"callback\","
                  "\"io_model\":\"one_context_per_connection\",\"dependency\":\""
#if defined(ARKNET_USE_BOOST_ASIO) && ARKNET_USE_BOOST_ASIO
               << "Boost.Asio"
#else
               << "Standalone Asio"
#endif
               << "\",\"tls\":" << (settings.tls ? "true" : "false") << ",\"round\":" << round
               << ",\"clients\":" << settings.clients << ",\"io_threads\":" << settings.clients
               << ",\"pipeline_commands\":" << settings.pipeline
               << ",\"inflight_batches_per_connection\":" << settings.window
               << ",\"payload_bytes\":" << settings.payload << ",\"elapsed_seconds\":" << elapsed
               << ",\"successful_commands\":" << successes << ",\"failed_commands\":" << failures
               << ",\"commands_per_second\":" << successes / elapsed
               << ",\"batch_roundtrip_p50_us\":" << percentile(0.50)
               << ",\"batch_roundtrip_p99_us\":" << percentile(0.99) << ",\"client_cpu_percent_of_one_core\":";
        if (cpu_measured_start && cpu_end)
        {
            output << (*cpu_end - *cpu_measured_start) / elapsed * 100;
        }
        else
        {
            output << "null";
        }
        output << ",\"process_peak_rss_bytes\":";
        if (const auto memory = peak_rss())
        {
            output << *memory;
        }
        else
        {
            output << "null";
        }
        output << ",\"latency_sample_count\":" << samples.size()
               << ",\"latency_sampling\":\"per_chain_reservoir_weighted_by_batches\",\"ok\":"
               << (!failures && successes ? "true" : "false") << "}\n";
        reports.push_back(output.str());
    }
    if (!passed)
    {
        std::cerr << (report ? "Measurement round " + std::to_string(round) : "Warmup") << " failed: " << failures
                  << " commands failed, " << successes << " commands succeeded";
        if (!failure_reason.empty())
        {
            std::cerr << " (" << failure_reason << ')';
        }
        std::cerr << '\n';
    }
    return passed;
}

template<class Client>
int run(const options& settings) {
    std::vector<std::shared_ptr<Client>> clients;
    arknet::redis::connect_options target;
    target.host = settings.host;
    target.port = settings.port;
    target.username = settings.username;
    target.client_name = "arknet_benchmark";
    if (const auto* password = std::getenv("ARKNET_REDIS_PASSWORD"))
    {
        target.password = password;
    }
    arknet::redis::client_options configuration;
    configuration.max_pending_commands = settings.pipeline * settings.window;
    configuration.max_pending_bytes = 64 * 1024 * 1024;
    configuration.command_timeout = std::chrono::seconds(5);
    configuration.tls_certificate_authority = settings.ca;
    for (std::size_t i = 0; i < settings.clients; ++i)
    {
        auto client = std::make_shared<Client>(configuration);
        auto completion = std::make_shared<std::promise<bool>>();
        auto connected = completion->get_future();
        client->async_connect(
            target, [completion](arknet::redis::result result) { completion->set_value(bool(result)); });
        clients.push_back(std::move(client));
        if (connected.wait_for(std::chrono::seconds(10)) != std::future_status::ready || !connected.get())
        {
            throw std::runtime_error("Redis connection or authentication failed");
        }
    }
    std::vector<std::string> reports;
    bool passed = settings.warmup == 0 || measure(settings, clients, settings.warmup, 0, false, reports);
    for (std::size_t round = 1; passed && round <= settings.rounds; ++round)
    {
        passed = measure(settings, clients, settings.seconds, round, true, reports);
    }
    for (const auto& client : clients)
    {
        client->close();
    }
    if (passed)
    {
        for (const auto& report : reports)
        {
            std::cout << report;
        }
    }
    return passed ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try
    {
        std::cout.imbue(std::locale::classic());
        const auto settings = arguments(argc, argv);
        if (settings.tls)
        {
#if defined(ARKNET_ENABLE_SSL)
            return run<arknet::redis::tls_client>(settings);
#else
            throw std::invalid_argument("TLS requires ARKNET_ENABLE_SSL");
#endif
        }
        return run<arknet::redis::client>(settings);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Redis benchmark: " << error.what() << '\n';
        return 1;
    }
}
