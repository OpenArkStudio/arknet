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
#include <variant>
#include <vector>

#include <arknet/mysql/connection.hpp>
#include "resources.hpp"

namespace {

using clock_type = std::chrono::steady_clock;

struct options
{
    arknet::mysql::connect_options connection;
    std::size_t clients = 4;
    std::size_t window = 1;
    std::size_t payload = 1024;
    std::size_t rounds = 3;
    double seconds = 1;
    double warmup = 0.25;
    bool prepared = true;
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
            std::cout
                << "arknet_mysql_benchmark --host HOST --port PORT --username USER --database DB "
                   "--ca FILE --plain --mode text|prepared --clients N --window N --payload BYTES "
                   "--rounds N --seconds SECONDS --warmup SECONDS\n"
                   "Password: ARKNET_MYSQL_PASSWORD environment variable. TLS verification is enabled by default.\n"
                   "Read-only workload: SELECT a binary literal, or prepared SELECT ? with binary bytes.\n"
                   "One IO context/thread per connection; one active command, remaining window queued.\n"
                   "JSON lines report successful queries/s and end-to-end query p50/p99 including queue time.\n"
                   "CPU is the client process only: one fully occupied core is 100%.\n";
            std::exit(0);
        }
        if (key == "--plain")
        {
            result.connection.tls = arknet::mysql::tls_mode::disabled;
            continue;
        }
        if (++i == argc)
        {
            throw std::invalid_argument("Missing value for " + key);
        }
        const std::string value = argv[i];
        if (key == "--host")
        {
            result.connection.host = value;
        }
        else if (key == "--port")
        {
            const auto port = integer(value);
            if (!port || port > 65535)
            {
                throw std::invalid_argument("Port must be in 1..65535");
            }
            result.connection.port = static_cast<std::uint16_t>(port);
        }
        else if (key == "--username")
        {
            result.connection.username = value;
        }
        else if (key == "--database")
        {
            result.connection.database = value;
        }
        else if (key == "--ca")
        {
            result.connection.ca_file = value;
        }
        else if (key == "--mode" && (value == "text" || value == "prepared"))
        {
            result.prepared = value == "prepared";
        }
        else if (key == "--clients")
        {
            result.clients = integer(value);
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
            throw std::invalid_argument("Unknown option or invalid value: " + key);
        }
    }
    if (result.connection.host.empty() || !result.clients || result.clients > 128 || !result.window ||
        result.window > 64 || result.payload > 1024 * 1024 || !result.rounds || result.rounds > 100 ||
        result.seconds <= 0 || result.seconds > 3600 || result.warmup < 0 || result.warmup > 3600 ||
        (result.connection.tls == arknet::mysql::tls_mode::disabled && !result.connection.ca_file.empty()))
    {
        throw std::invalid_argument("Invalid benchmark dimensions; use --help");
    }
    // Text mode encodes each payload byte as two hex digits. Include request,
    // response and retained parameter storage in the conservative budget.
    constexpr std::size_t budget = 64 * 1024 * 1024;
    if (result.window > budget / (result.payload * 4 + 4096))
    {
        throw std::invalid_argument("In-flight data exceeds the 64 MiB per-connection budget");
    }
    if (const auto* password = std::getenv("ARKNET_MYSQL_PASSWORD"))
    {
        result.connection.password = password;
    }
    result.connection.max_pending_commands = result.window;
    return result;
}

struct client_state
{
    arknet::mysql::connection connection;
    arknet::mysql::prepared_statement statement;
};

struct phase
{
    clock_type::time_point stop;
    std::atomic<std::size_t> remaining;
    std::promise<void> completion;

    explicit phase(std::size_t chains)
        : remaining(chains) {}
};

struct request_chain : std::enable_shared_from_this<request_chain>
{
    std::shared_ptr<client_state> client;
    std::shared_ptr<phase> measurement;
    std::shared_ptr<const std::string> payload;
    std::shared_ptr<const std::string> sql;
    bool prepared = false;
    std::size_t sample_capacity = 1;
    std::vector<double> samples;
    std::mt19937_64 random;
    std::uint64_t successes = 0;
    std::uint64_t failures = 0;

    void submit() {
        const auto began = clock_type::now();
        auto handler = [self = shared_from_this(), began](arknet::mysql::query_result response) {
            const auto* value = response && response.rows.size() == 1 && response.rows.front().size() == 1
                                    ? std::get_if<std::string>(&response.rows.front().front())
                                    : nullptr;
            const bool valid = value && *value == *self->payload;
            if (valid)
            {
                ++self->successes;
                const auto latency = std::chrono::duration<double, std::micro>(clock_type::now() - began).count();
                if (self->samples.size() < self->sample_capacity)
                {
                    self->samples.push_back(latency);
                }
                else
                {
                    const auto index =
                        std::uniform_int_distribution<std::uint64_t>(0, self->successes - 1)(self->random);
                    if (index < self->samples.size())
                    {
                        self->samples[static_cast<std::size_t>(index)] = latency;
                    }
                }
            }
            else
            {
                ++self->failures;
            }
            if (valid && clock_type::now() < self->measurement->stop)
            {
                self->submit();
            }
            else if (self->measurement->remaining.fetch_sub(1) == 1)
            {
                self->measurement->completion.set_value();
            }
        };
        if (prepared)
        {
            client->connection.async_execute(client->statement, {*payload}, std::move(handler));
        }
        else
        {
            client->connection.async_query(*sql, std::move(handler));
        }
    }
};

bool measure(const options& settings, const std::vector<std::shared_ptr<client_state>>& clients,
    const std::shared_ptr<const std::string>& payload, const std::shared_ptr<const std::string>& sql, double seconds,
    std::size_t round, bool report, std::ostream& output) {
    const auto count = clients.size() * settings.window;
    auto measurement = std::make_shared<phase>(count);
    auto finished = measurement->completion.get_future();
    std::vector<std::shared_ptr<request_chain>> chains;
    chains.reserve(count);
    for (const auto& client : clients)
    {
        for (std::size_t lane = 0; lane < settings.window; ++lane)
        {
            auto chain = std::make_shared<request_chain>();
            chain->client = client;
            chain->measurement = measurement;
            chain->payload = payload;
            chain->sql = sql;
            chain->prepared = settings.prepared;
            chain->sample_capacity = std::max<std::size_t>(1, 65536 / count);
            chain->samples.reserve(chain->sample_capacity);
            chain->random.seed(chains.size() + 1);
            chains.push_back(std::move(chain));
        }
    }
    const auto cpu_before = process_cpu();
    const auto began = clock_type::now();
    measurement->stop =
        began + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(seconds));
    for (const auto& chain : chains)
    {
        chain->submit();
    }
    if (finished.wait_for(std::chrono::duration<double>(seconds + 15)) != std::future_status::ready)
    {
        for (const auto& client : clients)
        {
            client->connection.close();
        }
        throw std::runtime_error("Benchmark phase exceeded its deadline");
    }
    const auto elapsed = std::chrono::duration<double>(clock_type::now() - began).count();
    const auto cpu_after = process_cpu();
    std::uint64_t successes = 0;
    std::uint64_t failures = 0;

    struct weighted_sample
    {
        double latency;
        double weight;
    };

    std::vector<weighted_sample> samples;
    for (const auto& chain : chains)
    {
        successes += chain->successes;
        failures += chain->failures;
        for (const auto sample : chain->samples)
        {
            samples.push_back({sample, double(chain->successes) / chain->samples.size()});
        }
    }
    std::sort(samples.begin(), samples.end(),
        [](const auto& left, const auto& right) { return left.latency < right.latency; });
    const auto percentile = [&](double fraction) {
        double weight = 0;
        for (const auto& sample : samples)
        {
            weight += sample.weight;
            if (weight >= fraction * double(successes))
            {
                return sample.latency;
            }
        }
        return samples.empty() ? 0 : samples.back().latency;
    };
    if (report)
    {
#if defined(ARKNET_USE_BOOST_ASIO) && ARKNET_USE_BOOST_ASIO
        constexpr const char* dependency = "boost";
#else
        constexpr const char* dependency = "standalone";
#endif
        output << std::fixed << std::setprecision(3) << "{\"protocol\":\"mysql\",\"dependency\":\"" << dependency
               << "\",\"mode\":\"" << (settings.prepared ? "prepared" : "text")
               << "\",\"tls\":" << (settings.connection.tls == arknet::mysql::tls_mode::required ? "true" : "false")
               << ",\"io_model\":\"one_context_thread_per_connection\",\"execution\":\"callback\",\"round\":" << round
               << ",\"clients\":" << clients.size() << ",\"window\":" << settings.window
               << ",\"payload_bytes\":" << settings.payload << ",\"wall_seconds\":" << elapsed
               << ",\"successful_queries\":" << successes << ",\"failed_queries\":" << failures
               << ",\"queries_per_second\":" << double(successes) / elapsed << ",\"query_p50_us\":" << percentile(0.50)
               << ",\"query_p99_us\":" << percentile(0.99) << ",\"latency_samples\":" << samples.size()
               << ",\"client_cpu_percent\":";
        if (cpu_before && cpu_after)
        {
            output << (*cpu_after - *cpu_before) / elapsed * 100;
        }
        else
        {
            output << "null";
        }
        output << ",\"process_peak_rss_bytes\":";
        if (const auto rss = peak_rss())
        {
            output << *rss;
        }
        else
        {
            output << "null";
        }
        output << "}\n";
    }
    return successes && !failures;
}

int run(const options& settings) {
    auto payload = std::make_shared<std::string>(settings.payload, '\0');
    auto sql = std::make_shared<std::string>("SELECT X'");
    constexpr std::string_view hex = "0123456789abcdef";
    for (std::size_t i = 0; i < payload->size(); ++i)
    {
        const auto byte = static_cast<unsigned char>(i % 256);
        (*payload)[i] = static_cast<char>(byte);
        sql->push_back(hex[byte >> 4]);
        sql->push_back(hex[byte & 15]);
    }
    sql->push_back('\'');
    std::vector<std::shared_ptr<client_state>> clients;
    for (std::size_t i = 0; i < settings.clients; ++i)
    {
        auto client = std::make_shared<client_state>();
        auto connected = std::make_shared<std::promise<arknet::mysql::connection_result>>();
        auto connection = connected->get_future();
        client->connection.async_connect(
            settings.connection, [connected](auto result) { connected->set_value(std::move(result)); });
        if (connection.wait_for(std::chrono::seconds(15)) != std::future_status::ready || !connection.get())
        {
            throw std::runtime_error("MySQL connection or authentication failed");
        }
        if (settings.prepared)
        {
            auto prepared = std::make_shared<std::promise<arknet::mysql::prepare_result>>();
            auto preparation = prepared->get_future();
            client->connection.async_prepare(
                "SELECT ?", [prepared](auto result) { prepared->set_value(std::move(result)); });
            if (preparation.wait_for(std::chrono::seconds(15)) != std::future_status::ready)
            {
                throw std::runtime_error("MySQL statement preparation timed out");
            }
            auto result = preparation.get();
            if (!result)
            {
                throw std::runtime_error("MySQL statement preparation failed");
            }
            client->statement = std::move(result.statement);
        }
        clients.push_back(std::move(client));
    }
    std::ostringstream records;
    records.imbue(std::locale::classic());
    if (settings.warmup > 0 && !measure(settings, clients, payload, sql, settings.warmup, 0, false, records))
    {
        throw std::runtime_error("Warmup failed; no valid performance results");
    }
    for (std::size_t round = 1; round <= settings.rounds; ++round)
    {
        if (!measure(settings, clients, payload, sql, settings.seconds, round, true, records))
        {
            throw std::runtime_error("Measurement failed; discard this invocation's results");
        }
    }
    for (const auto& client : clients)
    {
        client->connection.close();
    }
    std::cout << records.str();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try
    {
        std::cout.imbue(std::locale::classic());
        return run(arguments(argc, argv));
    }
    catch (const std::exception& error)
    {
        std::cerr << "MySQL benchmark: " << error.what() << '\n';
        return 1;
    }
}
