// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <arknet/rudp/rudp_client.hpp>
#include <arknet/rudp/rudp_server.hpp>
#include "impairment.hpp"
#include "resources.hpp"
#include "runtime.hpp"

namespace {

using clock_type = std::chrono::steady_clock;
constexpr std::size_t sample_limit = 8192;
constexpr auto drain_time = std::chrono::seconds(10);
constexpr int receive_buffer = 4 * 1024 * 1024;

struct options
{
    std::size_t payload = 1024;
    std::size_t clients = 1;
    std::size_t window = 1;
    std::size_t io_threads = 1;
    std::size_t rounds = 3;
    double seconds = 1;
    double warmup = 0.25;
    std::string io_model = "shared";
    std::string fec = "off", profile = "clean";
    arknet_benchmark::impairment_options impairment;
};

std::size_t integer(std::string_view text) {
    std::size_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
    {
        throw std::invalid_argument("invalid integer argument");
    }
    return result;
}

double duration(const std::string& text) {
    char* end{};
    const auto result = std::strtod(text.c_str(), &end);
    if (text.empty() || end != text.c_str() + text.size() || !std::isfinite(result))
    {
        throw std::invalid_argument("invalid duration argument");
    }
    return result;
}

options arguments(int argc, char** argv) {
    options result;
    bool explicit_profile = false, explicit_loss = false, explicit_burst = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        if (key == "--help")
        {
            std::cout << "arknet_rudp_benchmark --payload BYTES --clients COUNT --window COUNT "
                         "--seconds SECONDS --warmup SECONDS --rounds COUNT "
                         "--io-model shared|sharded --io-threads COUNT "
                         "--fec off|fixed|adaptive --profile clean|random|burst "
                         "--loss-percent PERCENT --burst-count COUNT --burst-period PACKETS "
                         "--delay-ms ONE_WAY_MS --duplicate-percent PERCENT --reorder-percent PERCENT "
                         "--seed INTEGER\n";
            std::exit(0);
        }
        if (++i == argc)
        {
            throw std::invalid_argument("missing value for " + key);
        }
        const std::string value = argv[i];
        if (key == "--payload")
        {
            result.payload = integer(value);
        }
        else if (key == "--clients")
        {
            result.clients = integer(value);
        }
        else if (key == "--window")
        {
            result.window = integer(value);
        }
        else if (key == "--io-threads")
        {
            result.io_threads = integer(value);
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
        else if (key == "--io-model")
        {
            result.io_model = value;
        }
        else if (key == "--fec")
        {
            result.fec = value;
        }
        else if (key == "--profile")
        {
            result.profile = value;
            explicit_profile = true;
        }
        else if (key == "--seed")
        {
            result.impairment.seed = integer(value);
        }
        else if (key == "--loss-percent" || key == "--duplicate-percent" || key == "--reorder-percent" ||
                 key == "--delay-ms" || key == "--burst-count" || key == "--burst-period")
        {
            const auto number = integer(value);
            if (number > 1000000)
            {
                throw std::invalid_argument("impairment argument out of range");
            }
            if (key == "--loss-percent")
            {
                result.impairment.loss_percent = static_cast<unsigned>(number);
                explicit_loss = true;
            }
            else if (key == "--duplicate-percent")
            {
                result.impairment.duplicate_percent = static_cast<unsigned>(number);
            }
            else if (key == "--reorder-percent")
            {
                result.impairment.reorder_percent = static_cast<unsigned>(number);
            }
            else if (key == "--delay-ms")
            {
                result.impairment.delay_ms = static_cast<unsigned>(number);
            }
            else if (key == "--burst-count")
            {
                result.impairment.burst_count = static_cast<unsigned>(number);
                explicit_burst = true;
            }
            else
            {
                result.impairment.burst_period = static_cast<unsigned>(number);
            }
        }
        else
        {
            throw std::invalid_argument("unknown argument: " + key);
        }
    }
    if (result.payload < 16 || result.payload > 16384 || result.clients < 1 || result.clients > 1024 ||
        result.window < 1 || result.window > 1024 || result.io_threads < 1 || result.io_threads > 64 ||
        result.rounds < 1 || result.rounds > 100 || result.seconds <= 0 || result.seconds > 3600 || result.warmup < 0 ||
        result.warmup > 3600)
    {
        throw std::invalid_argument("argument out of range");
    }
    if (std::uint64_t(result.clients) * result.window * result.payload > 256 * 1024 * 1024)
    {
        throw std::invalid_argument("requested outstanding payloads exceed 256 MiB");
    }
    if (result.io_model != "shared" && result.io_model != "sharded")
    {
        throw std::invalid_argument("unsupported IO model");
    }
    if (result.fec != "off" && result.fec != "fixed" && result.fec != "adaptive")
    {
        throw std::invalid_argument("unsupported FEC policy");
    }
    if (!explicit_profile)
    {
        result.profile = result.impairment.burst_count ? "burst" : result.impairment.loss_percent ? "random" : "clean";
    }
    if (result.profile != "clean" && result.profile != "random" && result.profile != "burst")
    {
        throw std::invalid_argument("unsupported impairment profile");
    }
    if (result.profile == "random" && !explicit_loss)
    {
        result.impairment.loss_percent = 3;
    }
    if (result.profile == "burst" && !explicit_burst)
    {
        result.impairment.burst_count = 3;
    }
    if (result.profile == "clean" && (result.impairment.loss_percent || result.impairment.burst_count))
    {
        throw std::invalid_argument("clean profile cannot include packet loss");
    }
    result.impairment.validate();
    return result;
}

arknet::fec::options fec_options(const options& settings) {
    return settings.fec == "fixed"      ? arknet::fec::options::fixed()
           : settings.fec == "adaptive" ? arknet::fec::options::adaptive()
                                        : arknet::fec::options{};
}

struct transport_totals
{
    arknet::rudp::statistics value;
    unsigned endpoints{}, parity_min = 16, parity_max{};
};

void add(transport_totals& totals, const arknet::rudp::statistics& value) {
    auto& sum = totals.value;
    sum.transmissions += value.transmissions;
    sum.retransmissions += value.retransmissions;
    sum.wire_bytes_tx += value.wire_bytes_tx;
    sum.wire_bytes_rx += value.wire_bytes_rx;
    sum.arq_pending_bytes += value.arq_pending_bytes;
    sum.raw_queued_bytes += value.raw_queued_bytes;
    sum.fec.data_packets_tx += value.fec.data_packets_tx;
    sum.fec.parity_packets_tx += value.fec.parity_packets_tx;
    sum.fec.parity_bytes_tx += value.fec.parity_bytes_tx;
    sum.fec.groups_rx += value.fec.groups_rx;
    sum.fec.data_shards_rx += value.fec.data_shards_rx;
    sum.fec.missing_shards += value.fec.missing_shards;
    sum.fec.recovered_shards += value.fec.recovered_shards;
    sum.fec.duplicates += value.fec.duplicates;
    sum.fec.invalid_packets += value.fec.invalid_packets;
    sum.fec.expired_groups += value.fec.expired_groups;
    sum.fec.evicted_groups += value.fec.evicted_groups;
    sum.fec.policy_changes += value.fec.policy_changes;
    sum.fec.retained_bytes += value.fec.retained_bytes;
    sum.fec.retained_groups += value.fec.retained_groups;
    ++totals.endpoints;
    totals.parity_min = std::min(totals.parity_min, static_cast<unsigned>(value.fec.current_parity_shards));
    totals.parity_max = std::max(totals.parity_max, static_cast<unsigned>(value.fec.current_parity_shards));
}

transport_totals snapshot(arknet::rudp_server& server, const std::vector<std::unique_ptr<arknet::rudp_client>>& clients,
    const arknet::fec::options& settings) {
    std::vector<std::shared_ptr<arknet::rudp_session>> sessions;
    server.foreach_session([&](auto& session) { sessions.push_back(session); });
    std::vector<std::future<arknet::rudp::statistics>> futures;
    for (const auto& client : clients)
    {
        futures.push_back(client->get_rudp_statistics());
    }
    for (const auto& session : sessions)
    {
        futures.push_back(session->get_rudp_statistics());
    }
    transport_totals totals;
    for (auto& future : futures)
    {
        if (future.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        {
            throw std::runtime_error("RUDP statistics snapshot timed out");
        }
        const auto value = future.get();
        if (value.fec.retained_bytes > settings.max_buffer_bytes || value.fec.retained_groups > settings.max_groups)
        {
            throw std::runtime_error("FEC retention limit exceeded");
        }
        if (value.arq_pending_bytes > arknet::arq::options{}.max_pending_bytes ||
            value.raw_queued_bytes > arknet::arq::options{}.max_pending_bytes)
        {
            throw std::runtime_error("RUDP queue limit exceeded");
        }
        add(totals, value);
    }
    return totals;
}

struct metrics
{
    explicit metrics(unsigned tag)
        : tag(tag) {
        samples.reserve(sample_limit);
    }

    unsigned tag;
    clock_type::time_point started{}, deadline{};
    double elapsed{};
    std::optional<double> cpu_percent;
    std::atomic<std::uint64_t> sent{}, messages{}, server_received{}, rejected{}, send_errors{};
    std::atomic<std::uint64_t> content_errors{}, order_errors{}, unexpected{}, disconnects{}, unfinished{};
    std::atomic<std::uint64_t> callback_exceptions{}, drain_timeouts{}, duplicate_completions{};
    std::atomic<std::size_t> remaining{}, pending_completions{};
    std::mutex done_mutex, samples_mutex;
    std::condition_variable done;
    std::vector<double> samples;
    std::uint64_t observations{};
    std::mt19937_64 random{0x61726b6e6574ULL};

    void notify() noexcept {
        std::lock_guard lock(done_mutex);
        done.notify_all();
    }

    void observe(double microseconds) {
        std::lock_guard lock(samples_mutex);
        ++observations;
        if (samples.size() < sample_limit)
        {
            samples.push_back(microseconds);
        }
        else
        {
            const auto index = std::uniform_int_distribution<std::uint64_t>(0, observations - 1)(random);
            if (index < sample_limit)
            {
                samples[static_cast<std::size_t>(index)] = microseconds;
            }
        }
    }

    std::uint64_t failures() const noexcept {
        return rejected + send_errors + content_errors + order_errors + unexpected + disconnects + unfinished +
               callback_exceptions + drain_timeouts + duplicate_completions;
    }
};

struct round_state
{
    std::array<std::shared_ptr<metrics>, 2> phases{std::make_shared<metrics>(0), std::make_shared<metrics>(1)};
    std::atomic<bool> closing{};
    int server_receive_buffer{};
    std::vector<int> client_receive_buffers;
    transport_totals before, after;
    arknet_benchmark::impairment_statistics relay_before, relay_after;
    std::size_t fec_retained_peak{}, fec_groups_peak{}, arq_pending_peak{}, raw_queue_peak{};
    unsigned parity_observed_max{};
    std::string error;
};

template<class Sender>
bool send(Sender& sender, std::string message, const std::shared_ptr<metrics>& state) {
    const auto size = message.size();
    auto completed = std::make_shared<std::atomic<bool>>(false);
    ++state->pending_completions;
    const auto completion = [state, completed, size](const arknet::error_code& error, std::size_t bytes) noexcept {
        if (completed->exchange(true))
        {
            ++state->duplicate_completions;
            return;
        }
        if (error || bytes != size)
        {
            ++state->send_errors;
        }
        if (state->pending_completions.fetch_sub(1) == 1)
        {
            state->notify();
        }
    };
    try
    {
        const bool accepted = sender.async_send(std::move(message), completion);
        if (!accepted)
        {
            ++state->rejected;
        }
        return accepted;
    }
    catch (...)
    {
        ++state->callback_exceptions;
        completion(asio::error::operation_aborted, 0);
        return false;
    }
}

void put_integer(std::string& message, std::size_t offset, std::uint64_t value, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i)
    {
        message[offset + i] = static_cast<char>((value >> ((bytes - 1 - i) * 8)) & 255);
    }
}

std::uint64_t get_integer(std::string_view message, std::size_t offset, std::size_t bytes) {
    std::uint64_t value{};
    for (std::size_t i = 0; i < bytes; ++i)
    {
        value = (value << 8) | static_cast<unsigned char>(message[offset + i]);
    }
    return value;
}

struct client_state
{
    struct pending
    {
        std::string message;
        clock_type::time_point sent;
        std::uint64_t sequence{};
    };

    client_state(
        arknet::rudp_client& client, std::shared_ptr<round_state> owner, const options& settings, std::uint32_t id)
        : client(client)
        , owner(std::move(owner))
        , settings(settings)
        , id(id)
        , slots(settings.window) {}

    arknet::rudp_client& client;
    std::shared_ptr<round_state> owner;
    const options& settings;
    std::uint32_t id;
    std::vector<pending> slots;
    std::shared_ptr<metrics> phase;
    std::uint64_t next_sequence{}, expected_sequence{};
    std::size_t inflight{};
    bool stopped{}, finished{};

    void finish(bool force = false) noexcept {
        if (!phase || finished)
        {
            return;
        }
        if (force)
        {
            phase->unfinished += inflight;
            for (auto& slot : slots)
            {
                slot.message.clear();
            }
            inflight = 0;
        }
        if (inflight)
        {
            return;
        }
        finished = true;
        if (phase->remaining.fetch_sub(1) == 1)
        {
            phase->notify();
        }
    }

    void transmit(std::size_t index) {
        if (stopped || clock_type::now() >= phase->deadline)
        {
            return;
        }
        auto& slot = slots[index];
        slot.message.assign(settings.payload, '\0');
        slot.message.replace(0, 3, "ANR");
        slot.message[3] = static_cast<char>(phase->tag);
        slot.sequence = next_sequence++;
        put_integer(slot.message, 4, id, 4);
        put_integer(slot.message, 8, slot.sequence, 8);
        for (std::size_t i = 16; i < slot.message.size(); ++i)
        {
            slot.message[i] = static_cast<char>((slot.sequence + i * 17 + std::uint64_t(id) * 31) & 255);
        }
        slot.sent = clock_type::now();
        ++inflight;
        if (send(client, slot.message, phase))
        {
            ++phase->sent;
        }
        else
        {
            slot.message.clear();
            --inflight;
            stopped = true;
        }
    }

    void begin(const std::shared_ptr<metrics>& current) noexcept {
        phase = current;
        stopped = finished = false;
        expected_sequence = next_sequence;
        try
        {
            if (!client.is_started())
            {
                ++phase->disconnects;
                stopped = true;
                finish(true);
                return;
            }
            for (std::size_t i = 0; i < slots.size(); ++i)
            {
                transmit(i);
            }
            if (stopped)
            {
                finish();
            }
        }
        catch (...)
        {
            ++phase->callback_exceptions;
            stopped = true;
            finish(true);
        }
    }

    void receive(std::string_view message) noexcept {
        try
        {
            if (!phase || message.size() != settings.payload || message.substr(0, 3) != "ANR" ||
                static_cast<unsigned char>(message[3]) != phase->tag || get_integer(message, 4, 4) != id)
            {
                ++(phase ? phase : owner->phases[0])->content_errors;
                return;
            }
            const auto sequence = get_integer(message, 8, 8);
            auto found = std::find_if(slots.begin(), slots.end(),
                [sequence](const pending& slot) { return !slot.message.empty() && slot.sequence == sequence; });
            if (finished || found == slots.end())
            {
                ++phase->unexpected;
                return;
            }
            const auto now = clock_type::now();
            if (message != found->message)
            {
                ++phase->content_errors;
            }
            else
            {
                if (sequence != expected_sequence)
                {
                    ++phase->order_errors;
                }
                expected_sequence = sequence + 1;
                ++phase->messages;
                phase->observe(std::chrono::duration<double, std::micro>(now - found->sent).count());
            }
            const auto index = static_cast<std::size_t>(found - slots.begin());
            found->message.clear();
            --inflight;
            if (now < phase->deadline)
            {
                transmit(index);
            }
            finish();
        }
        catch (...)
        {
            ++(phase ? phase : owner->phases[0])->callback_exceptions;
            stopped = true;
        }
    }

    void disconnect() noexcept {
        if (!owner->closing && phase)
        {
            ++phase->disconnects;
            stopped = true;
            finish(true);
        }
    }
};

void run_phase(const std::vector<std::shared_ptr<client_state>>& clients, const std::shared_ptr<metrics>& phase,
    double seconds, const std::function<void()>& sample = {}) {
    if (seconds == 0)
    {
        return;
    }
    phase->remaining = clients.size();
    phase->started = clock_type::now();
    phase->deadline =
        phase->started + std::chrono::duration_cast<clock_type::duration>(std::chrono::duration<double>(seconds));
    const auto cpu_start = process_cpu();
    for (const auto& state : clients)
    {
        state->client.post([state, phase] { state->begin(phase); });
    }
    while (clock_type::now() < phase->deadline)
    {
        std::this_thread::sleep_until(std::min(phase->deadline, clock_type::now() + std::chrono::milliseconds(100)));
        if (sample)
        {
            sample();
        }
    }
    for (const auto& state : clients)
    {
        state->client.post([state] {
            state->stopped = true;
            state->finish();
        });
    }
    {
        std::unique_lock lock(phase->done_mutex);
        if (!phase->done.wait_until(lock, phase->deadline + drain_time,
                [&] { return phase->remaining == 0 && phase->pending_completions == 0; }))
        {
            ++phase->drain_timeouts;
        }
    }
    std::vector<std::future<void>> barriers;
    for (const auto& state : clients)
    {
        barriers.push_back(state->client.post(
            [state] {
                state->stopped = true;
                state->finish(true);
            },
            asio::use_future));
    }
    for (auto& barrier : barriers)
    {
        if (barrier.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
        {
            throw std::runtime_error("client IO barrier timed out");
        }
        barrier.get();
    }
    phase->elapsed = std::chrono::duration<double>(clock_type::now() - phase->started).count();
    const auto cpu_end = process_cpu();
    if (cpu_start && cpu_end && phase->elapsed > 0)
    {
        phase->cpu_percent = (*cpu_end - *cpu_start) / phase->elapsed * 100;
    }
}

int configure(asio::ip::udp::socket& socket, const std::shared_ptr<round_state>& owner) noexcept {
    arknet::error_code error;
    socket.set_option(asio::socket_base::receive_buffer_size(receive_buffer), error);
    if (error)
    {
        ++owner->phases[0]->callback_exceptions;
        return 0;
    }
    asio::socket_base::receive_buffer_size effective;
    socket.get_option(effective, error);
    if (error)
    {
        ++owner->phases[0]->callback_exceptions;
        return 0;
    }
    return effective.value();
}

std::shared_ptr<round_state> benchmark(const options& settings, std::size_t round) {
    auto owner = std::make_shared<round_state>();
    owner->client_receive_buffers.resize(settings.clients);
    benchmark_runtime runtime(settings.io_model, settings.io_threads);
    auto impairment = settings.impairment;
    impairment.seed += round;
    arknet_benchmark::udp_impairment relay(impairment);
    arknet::rudp_server server(1024, 128 * 1024, runtime.server_lanes(settings.clients));
    std::vector<std::unique_ptr<arknet::rudp_client>> clients;
    std::vector<std::shared_ptr<client_state>> states;
    const auto stop = [&] {
        owner->closing = true;
        for (auto& client : clients)
        {
            client->stop();
        }
        server.stop();
        relay.stop();
    };
    try
    {
        server.set_rudp_options(arknet::arq::options{});
        server.set_fec_options(fec_options(settings));
        server.bind_init([&server, owner] { owner->server_receive_buffer = configure(server.acceptor(), owner); });
        server.bind_recv([owner](auto& session, std::string_view message) noexcept {
            try
            {
                const auto tag = message.size() >= 16 ? static_cast<unsigned char>(message[3]) : 2;
                const auto state = owner->phases[tag < 2 ? tag : 0];
                if (tag > 1 || message.substr(0, 3) != "ANR")
                {
                    ++state->content_errors;
                    return;
                }
                ++state->server_received;
                send(*session, std::string(message), state);
            }
            catch (...)
            { ++owner->phases[0]->callback_exceptions; }
        });
        if (!server.start("127.0.0.1", 0))
        {
            throw std::runtime_error("server start failed: " + arknet::get_last_error().message());
        }
        for (std::size_t i = 0; i < settings.clients; ++i)
        {
            auto client = std::make_unique<arknet::rudp_client>(1024, 128 * 1024, runtime.context_at(i));
            auto state = std::make_shared<client_state>(*client, owner, settings, static_cast<std::uint32_t>(i));
            client->set_rudp_options(arknet::arq::options{});
            client->set_fec_options(fec_options(settings));
            client->set_auto_reconnect(false);
            client->set_connect_timeout(std::chrono::seconds(10));
            client->bind_init([state] {
                state->owner->client_receive_buffers[state->id] = configure(state->client.socket(), state->owner);
            });
            client->bind_recv([state](std::string_view message) noexcept { state->receive(message); });
            client->bind_disconnect([state]() noexcept { state->disconnect(); });
            clients.push_back(std::move(client));
            states.push_back(std::move(state));
            const auto port = relay.add({asio::ip::make_address("127.0.0.1"), server.get_listen_port()});
            if (!clients.back()->start("127.0.0.1", port))
            {
                throw std::runtime_error("client start failed: " + arknet::get_last_error().message());
            }
        }
        run_phase(states, owner->phases[0], settings.warmup);
        owner->before = snapshot(server, clients, fec_options(settings));
        owner->relay_before = relay.snapshot(true);
        const auto sample = [&] {
            owner->after = snapshot(server, clients, fec_options(settings));
            owner->fec_retained_peak = std::max(owner->fec_retained_peak, owner->after.value.fec.retained_bytes);
            owner->fec_groups_peak = std::max(owner->fec_groups_peak, owner->after.value.fec.retained_groups);
            owner->arq_pending_peak = std::max(owner->arq_pending_peak, owner->after.value.arq_pending_bytes);
            owner->raw_queue_peak = std::max(owner->raw_queue_peak, owner->after.value.raw_queued_bytes);
            owner->parity_observed_max = std::max(owner->parity_observed_max, owner->after.parity_max);
        };
        sample();
        run_phase(states, owner->phases[1], settings.seconds, sample);
        sample();
        owner->relay_after = relay.snapshot();
    }
    catch (const std::exception& error)
    { owner->error = error.what(); }
    catch (...)
    { owner->error = "unknown benchmark failure"; }
    stop();
    return owner;
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty())
    {
        return 0;
    }
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
}

double median(std::vector<double> values) {
    if (values.empty())
    {
        return 0;
    }
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

void write_quoted(std::string_view value) {
    std::cout << '"';
    for (const unsigned char c : value)
    {
        if (c == '"' || c == '\\')
        {
            std::cout << '\\' << static_cast<char>(c);
        }
        else if (c < 32)
        {
            std::cout << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
        }
        else
        {
            std::cout << static_cast<char>(c);
        }
    }
    std::cout << '"';
}

void optional_number(const std::optional<double>& value) {
    if (value)
    {
        std::cout << *value;
    }
    else
    {
        std::cout << "null";
    }
}

bool output(const options& settings, const std::vector<std::shared_ptr<round_state>>& rounds) {
    std::vector<double> rates, p50s, p99s, cpu, wire_rates, overheads;
    std::uint64_t failures{};
    bool ok = true;
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(12)
              << "{\"schema_version\":1,\"protocol\":\"rudp\",\"wire_protocol\":\"kcp\",\"api\":\"callback\","
                 "\"backend\":";
#if ARKNET_USE_BOOST_ASIO
    std::cout << "\"boost\"";
#else
    std::cout << "\"standalone\"";
#endif
    std::cout << ",\"payload_bytes\":" << settings.payload << ",\"clients\":" << settings.clients
              << ",\"window\":" << settings.window << ",\"io_model\":";
    write_quoted(settings.io_model);
    std::cout << ",\"fec_policy\":";
    write_quoted(settings.fec);
    std::cout << ",\"fec_envelope\":";
    write_quoted(settings.fec == "off" ? "none" : "afec-v1");
    std::cout << ",\"impairment_profile\":";
    write_quoted(settings.profile);
    const auto& impairment = settings.impairment;
    std::cout << ",\"loss_percent\":" << impairment.loss_percent << ",\"burst_count\":" << impairment.burst_count
              << ",\"burst_period_packets\":" << impairment.burst_period
              << ",\"one_way_delay_ms\":" << impairment.delay_ms
              << ",\"duplicate_percent\":" << impairment.duplicate_percent
              << ",\"reorder_percent\":" << impairment.reorder_percent
              << ",\"reorder_extra_delay_ms\":" << impairment.delay_ms + 5 << ",\"seed\":" << impairment.seed
              << ",\"relay_io_contexts\":1,\"relay_io_threads\":1,\"statistics_sample_interval_ms\":100"
                 ",\"cpu_scope\":\"process: clients, server, relay, statistics sampling\""
                 ",\"wire_bytes_scope\":\"endpoint UDP payloads; excludes IP and UDP headers\""
                 ",\"protocol_statistics_scope\":\"post-warmup baseline through drained final snapshot\""
              << ",\"relay_queue_limit_bytes_per_route\":" << arknet_benchmark::udp_impairment::max_buffer_bytes
              << ",\"relay_queue_limit_packets_per_route\":" << arknet_benchmark::udp_impairment::max_packets;
    const arknet::arq::options transport;
    const auto fec = fec_options(settings);
    std::cout << ",\"io_contexts\":" << (settings.io_model == "shared" ? 1 : settings.io_threads)
              << ",\"io_threads\":" << settings.io_threads << ",\"round_count\":" << settings.rounds
              << ",\"requested_seconds\":" << settings.seconds << ",\"warmup_seconds\":" << settings.warmup
              << ",\"arq_mtu_bytes\":" << transport.mtu - (settings.fec == "off" ? 0 : arknet::fec::transport::overhead)
              << ",\"udp_wire_mtu_bytes\":" << transport.mtu
              << ",\"arq_send_window_segments\":" << transport.send_window
              << ",\"arq_receive_window_segments\":" << transport.receive_window
              << ",\"arq_update_interval_ms\":" << transport.interval
              << ",\"arq_congestion_control\":" << (transport.congestion_control ? "true" : "false")
              << ",\"udp_receive_buffer_requested_bytes\":" << receive_buffer
              << ",\"fec_data_shards\":" << unsigned(fec.data_shards)
              << ",\"fec_fixed_parity_shards\":" << unsigned(fec.parity_shards)
              << ",\"fec_max_parity_shards\":" << unsigned(fec.max_parity_shards)
              << ",\"fec_max_redundancy_percent\":" << fec.max_redundancy_percent
              << ",\"fec_group_wait_ms\":" << fec.group_wait_ms
              << ",\"fec_retention_limit_bytes_per_endpoint\":" << fec.max_buffer_bytes
              << ",\"cpu_percent_per_logical_core\":100,\"rounds\":[";
    for (std::size_t i = 0; i < rounds.size(); ++i)
    {
        if (i)
        {
            std::cout << ',';
        }
        const auto& round = rounds[i];
        const auto& state = round->phases[1];
        const auto buffers =
            std::minmax_element(round->client_receive_buffers.begin(), round->client_receive_buffers.end());
        const auto minimum_buffer = round->client_receive_buffers.empty() ? 0 : *buffers.first;
        const auto maximum_buffer = round->client_receive_buffers.empty() ? 0 : *buffers.second;
        const auto messages = state->messages.load();
        const auto& before = round->before.value;
        const auto& after = round->after.value;
        const auto& relay_before = round->relay_before;
        const auto& relay_after = round->relay_after;
        const auto delta = [](std::uint64_t end, std::uint64_t begin) { return end >= begin ? end - begin : 0; };
        const auto wire_bytes = delta(after.wire_bytes_tx, before.wire_bytes_tx);
        const auto payload_bytes = messages * settings.payload * 2;
        const double overhead = payload_bytes ? (double(wire_bytes) / double(payload_bytes) - 1) * 100 : 0;
        const auto errors =
            state->failures() + round->phases[0]->failures() + relay_after.errors + relay_after.overflow_drops;
        const auto rate = state->elapsed > 0 ? messages / state->elapsed : 0;
        const auto wire_rate = state->elapsed > 0 ? double(wire_bytes) * 8 / state->elapsed / 1000000 : 0;
        const auto p50 = percentile(state->samples, 0.5);
        const auto p99 = percentile(state->samples, 0.99);
        const bool passed = errors == 0 && round->error.empty() && messages > 0 && state->pending_completions == 0 &&
                            state->sent == messages && state->server_received == messages;
        const auto round_failures = errors != 0 ? errors : (passed ? 0 : 1);
        failures += round_failures;
        ok = ok && passed;
        if (passed)
        {
            rates.push_back(rate);
            p50s.push_back(p50);
            p99s.push_back(p99);
            if (state->cpu_percent)
            {
                cpu.push_back(*state->cpu_percent);
            }
            wire_rates.push_back(wire_rate);
            overheads.push_back(overhead);
        }
        std::cout << "{\"round\":" << i + 1 << ",\"elapsed_seconds\":" << state->elapsed
                  << ",\"messages_sent\":" << state->sent << ",\"messages\":" << messages
                  << ",\"server_received_messages\":" << state->server_received << ",\"roundtrips_per_second\":" << rate
                  << ",\"payload_roundtrip_mbit_per_second\":" << rate * settings.payload * 2 * 8 / 1000000
                  << ",\"rtt_p50_us\":" << p50 << ",\"rtt_p99_us\":" << p99
                  << ",\"rtt_samples\":" << state->samples.size() << ",\"rtt_observations\":" << state->observations
                  << ",\"udp_server_receive_buffer_bytes\":" << round->server_receive_buffer
                  << ",\"udp_client_receive_buffer_min_bytes\":" << minimum_buffer
                  << ",\"udp_client_receive_buffer_max_bytes\":" << maximum_buffer << ",\"cpu_percent\":";
        optional_number(state->cpu_percent);
        const auto relay_packets = delta(relay_after.received_packets, relay_before.received_packets);
        const auto relay_drops = delta(relay_after.random_drops, relay_before.random_drops) +
                                 delta(relay_after.burst_drops, relay_before.burst_drops);
        std::cout
            << ",\"seed\":" << impairment.seed + i << ",\"wire_bytes_tx\":" << wire_bytes
            << ",\"wire_bytes_rx\":" << delta(after.wire_bytes_rx, before.wire_bytes_rx)
            << ",\"wire_tx_mbit_per_second\":" << wire_rate << ",\"wire_payload_overhead_percent\":" << overhead
            << ",\"arq_transmissions\":" << delta(after.transmissions, before.transmissions)
            << ",\"arq_retransmissions\":" << delta(after.retransmissions, before.retransmissions)
            << ",\"fec_data_packets_tx\":" << delta(after.fec.data_packets_tx, before.fec.data_packets_tx)
            << ",\"fec_parity_packets_tx\":" << delta(after.fec.parity_packets_tx, before.fec.parity_packets_tx)
            << ",\"fec_parity_wire_bytes_tx\":" << delta(after.fec.parity_bytes_tx, before.fec.parity_bytes_tx)
            << ",\"fec_missing_shards\":" << delta(after.fec.missing_shards, before.fec.missing_shards)
            << ",\"fec_recovered_shards\":" << delta(after.fec.recovered_shards, before.fec.recovered_shards)
            << ",\"fec_duplicates\":" << delta(after.fec.duplicates, before.fec.duplicates)
            << ",\"fec_invalid_packets\":" << delta(after.fec.invalid_packets, before.fec.invalid_packets)
            << ",\"fec_expired_groups\":" << delta(after.fec.expired_groups, before.fec.expired_groups)
            << ",\"fec_evicted_groups\":" << delta(after.fec.evicted_groups, before.fec.evicted_groups)
            << ",\"fec_policy_changes\":" << delta(after.fec.policy_changes, before.fec.policy_changes)
            << ",\"fec_current_parity_min\":" << round->after.parity_min
            << ",\"fec_current_parity_max\":" << round->after.parity_max
            << ",\"fec_observed_parity_max\":" << round->parity_observed_max
            << ",\"fec_retained_sampled_peak_bytes\":" << round->fec_retained_peak
            << ",\"fec_groups_sampled_peak\":" << round->fec_groups_peak
            << ",\"arq_pending_sampled_peak_bytes\":" << round->arq_pending_peak
            << ",\"raw_queue_sampled_peak_bytes\":" << round->raw_queue_peak
            << ",\"relay_received_packets\":" << relay_packets
            << ",\"relay_received_bytes\":" << delta(relay_after.received_bytes, relay_before.received_bytes)
            << ",\"relay_forwarded_packets\":" << delta(relay_after.forwarded_packets, relay_before.forwarded_packets)
            << ",\"relay_forwarded_bytes\":" << delta(relay_after.forwarded_bytes, relay_before.forwarded_bytes)
            << ",\"relay_random_drops\":" << delta(relay_after.random_drops, relay_before.random_drops)
            << ",\"relay_burst_drops\":" << delta(relay_after.burst_drops, relay_before.burst_drops)
            << ",\"relay_loss_percent\":" << (relay_packets ? double(relay_drops) / double(relay_packets) * 100 : 0)
            << ",\"relay_duplicate_packets\":" << delta(relay_after.duplicate_packets, relay_before.duplicate_packets)
            << ",\"relay_reordered_packets\":" << delta(relay_after.reordered_packets, relay_before.reordered_packets)
            << ",\"relay_sum_route_peak_bytes\":" << relay_after.peak_retained_bytes
            << ",\"relay_sum_route_peak_packets\":" << relay_after.peak_retained_packets
            << ",\"relay_overflow_drops\":" << delta(relay_after.overflow_drops, relay_before.overflow_drops)
            << ",\"relay_errors\":" << delta(relay_after.errors, relay_before.errors);
        std::cout << ",\"failures\":" << round_failures << ",\"content_errors\":" << state->content_errors
                  << ",\"order_errors\":" << state->order_errors << ",\"unexpected_messages\":" << state->unexpected
                  << ",\"unfinished_messages\":" << state->unfinished << ",\"send_rejections\":" << state->rejected
                  << ",\"send_completion_errors\":" << state->send_errors
                  << ",\"disconnect_errors\":" << state->disconnects << ",\"drain_timeouts\":" << state->drain_timeouts
                  << ",\"callback_exceptions\":" << state->callback_exceptions
                  << ",\"duplicate_completions\":" << state->duplicate_completions
                  << ",\"pending_completions\":" << state->pending_completions
                  << ",\"warmup_failures\":" << round->phases[0]->failures()
                  << ",\"ok\":" << (passed ? "true" : "false") << ",\"error\":";
        if (round->error.empty())
        {
            std::cout << (passed ? "null" : "\"benchmark validation failed\"");
        }
        else
        {
            write_quoted(round->error);
        }
        std::cout << '}';
    }
    const auto median_rate = median(rates);
    std::cout << "],\"summary\":{\"successful_rounds\":" << rates.size()
              << ",\"roundtrips_per_second_median\":" << median_rate << ",\"roundtrips_per_second_min\":"
              << (rates.empty() ? 0 : *std::min_element(rates.begin(), rates.end()))
              << ",\"roundtrips_per_second_max\":"
              << (rates.empty() ? 0 : *std::max_element(rates.begin(), rates.end()))
              << ",\"payload_roundtrip_mbit_per_second_median\":" << median_rate * settings.payload * 2 * 8 / 1000000
              << ",\"rtt_p50_us_median\":" << median(p50s) << ",\"rtt_p99_us_median\":" << median(p99s)
              << ",\"cpu_percent_median\":";
    optional_number(cpu.empty() ? std::nullopt : std::optional<double>(median(cpu)));
    std::cout << ",\"wire_tx_mbit_per_second_median\":" << median(wire_rates)
              << ",\"wire_payload_overhead_percent_median\":" << median(overheads);
    std::cout << ",\"failures\":" << failures << "},\"peak_rss_bytes\":";
    if (const auto rss = peak_rss())
    {
        std::cout << *rss;
    }
    else
    {
        std::cout << "null";
    }
    std::cout << ",\"ok\":" << (ok ? "true" : "false") << "}\n";
    return ok;
}
} // namespace

int main(int argc, char** argv) {
    try
    {
        const auto settings = arguments(argc, argv);
        std::vector<std::shared_ptr<round_state>> rounds;
        for (std::size_t i = 0; i < settings.rounds; ++i)
        {
            rounds.push_back(benchmark(settings, i));
        }
        return output(settings, rounds) ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "RUDP benchmark: " << error.what() << '\n';
        return 1;
    }
}
