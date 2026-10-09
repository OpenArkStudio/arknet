#include <arknet/arknet.hpp>
#include "runtime.hpp"
#include "resources.hpp"
#include "work.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
using clock_type = std::chrono::steady_clock;
constexpr std::size_t sample_limit = 8192;
constexpr auto drain_time = std::chrono::seconds(2);
constexpr int udp_send_buffer = 65536;
constexpr int udp_receive_buffer = 4 * 1024 * 1024;

struct options
{
    std::string protocol = "tcp";
    std::size_t payload = 1024;
    std::size_t clients = 1;
    std::size_t window = 1;
    double seconds = 1;
    double warmup = 0.25;
    std::string certs;
    std::string io_model = "shared";
    std::size_t io_threads = 1;
    std::size_t work = 0;
};

std::size_t integer(std::string_view value)
{
    std::size_t parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
        throw std::invalid_argument("invalid integer argument");
    return parsed;
}

double duration(const std::string& value)
{
    char* end{};
    const double parsed = std::strtod(value.c_str(), &end);
    if (value.empty() || end != value.c_str() + value.size() || !std::isfinite(parsed))
        throw std::invalid_argument("invalid duration argument");
    return parsed;
}

options arguments(int argc, char** argv)
{
    options result;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        if (key == "--help")
        {
            std::cerr << "arknet_loopback_benchmark --protocol tcp|udp|websocket|tcps|wss|http|https "
                "--payload BYTES --clients COUNT --window COUNT --seconds SECONDS "
                "--warmup SECONDS --certs DIRECTORY --io-model shared|sharded|owned --io-threads COUNT "
                "--work ITERATIONS\n";
            std::exit(0);
        }
        if (++i == argc)
            throw std::invalid_argument("missing value for " + key);
        const std::string value = argv[i];
        if (key == "--protocol") result.protocol = value;
        else if (key == "--payload") result.payload = integer(value);
        else if (key == "--clients") result.clients = integer(value);
        else if (key == "--window") result.window = integer(value);
        else if (key == "--seconds") result.seconds = duration(value);
        else if (key == "--warmup") result.warmup = duration(value);
        else if (key == "--certs") result.certs = value;
        else if (key == "--io-model") result.io_model = value;
        else if (key == "--io-threads") result.io_threads = integer(value);
        else if (key == "--work") result.work = integer(value);
        else throw std::invalid_argument("unknown argument: " + key);
    }
    if (result.protocol != "tcp" && result.protocol != "udp" && result.protocol != "websocket" &&
        result.protocol != "tcps" && result.protocol != "wss" && result.protocol != "http" && result.protocol != "https")
        throw std::invalid_argument("unsupported protocol");
    if (result.payload < 16 || result.payload > 65507 || result.clients < 1 || result.clients > 1024 ||
        result.window < 1 || result.window > 1024 || result.seconds <= 0 || result.seconds > 3600 ||
        result.warmup < 0 || result.warmup > 3600)
        throw std::invalid_argument("argument out of range");
    if (std::uint64_t(result.clients) * result.window * result.payload > 256 * 1024 * 1024)
        throw std::invalid_argument("requested outstanding payloads exceed 256 MiB");
    if (result.io_model != "shared" && result.io_model != "sharded" && result.io_model != "owned")
        throw std::invalid_argument("unsupported IO model");
    if (result.io_threads < 1 || result.io_threads > 64 ||
        (result.io_model == "owned" && result.io_threads != 1))
        throw std::invalid_argument("invalid IO thread count; owned uses one worker per context");
    if (result.work > 10000000) throw std::invalid_argument("handler work exceeds limit");
    if ((result.protocol == "tcps" || result.protocol == "wss" || result.protocol == "https") && result.certs.empty())
        throw std::invalid_argument("TLS protocols require --certs");
    return result;
}

struct metrics
{
    explicit metrics(unsigned tag_value) : tag(tag_value) { samples.reserve(sample_limit); }
    unsigned tag;
    clock_type::time_point started{}, deadline{};
    double elapsed{};
    std::optional<double> cpu;
    std::atomic<std::uint64_t> sent{}, messages{}, client_rejections{}, client_completion_errors{};
    std::atomic<std::uint64_t> server_rejections{}, server_completion_errors{}, content_errors{}, order_errors{};
    std::atomic<std::uint64_t> unexpected{}, disconnects{}, udp_lost{}, unfinished{}, callback_exceptions{};
    std::atomic<std::uint64_t> drain_timeouts{}, duplicate_completions{};
    std::atomic<std::size_t> remaining{}, pending_completions{};
    std::mutex done_mutex;
    std::condition_variable done;
    std::mutex samples_mutex;
    std::mutex failure_mutex;
    std::string first_failure;
    std::vector<double> samples;
    std::uint64_t observations{};
    std::mt19937_64 random{0x61726b6e6574ULL};

    void notify() noexcept
    {
        std::lock_guard lock(done_mutex);
        done.notify_all();
    }

    void record_failure(const char* operation, const arknet::error_code& error, std::size_t bytes = 0,
        std::size_t expected = 0) noexcept
    {
        try
        {
            std::lock_guard lock(failure_mutex);
            if (!first_failure.empty()) return;
            first_failure = operation;
            if (error) first_failure += error.message();
            else first_failure += "completion byte count " + std::to_string(bytes) + " != " + std::to_string(expected);
        }
        catch (...) { ++callback_exceptions; }
    }

    void observe(double microseconds)
    {
        std::lock_guard lock(samples_mutex);
        ++observations;
        if (samples.size() < sample_limit)
            samples.push_back(microseconds);
        else
        {
            const auto index = std::uniform_int_distribution<std::uint64_t>(0, observations - 1)(random);
            if (index < sample_limit)
                samples[static_cast<std::size_t>(index)] = microseconds;
        }
    }

    std::uint64_t errors() const noexcept
    {
        return client_rejections + client_completion_errors + server_rejections + server_completion_errors +
            content_errors + order_errors + unexpected + disconnects + udp_lost + unfinished + callback_exceptions +
            drain_timeouts + duplicate_completions;
    }
};

struct context
{
    std::array<std::shared_ptr<metrics>, 2> phases{
        std::make_shared<metrics>(0), std::make_shared<metrics>(1)};
    std::atomic<bool> closing{};
    std::array<int, 2> server_buffers{};
    std::vector<std::array<int, 2>> client_buffers;
};

std::array<int, 2> configure_udp(asio::ip::udp::socket& socket, const std::shared_ptr<context>& owner) noexcept
{
    arknet::error_code error;
    const auto failure = [&](const char* operation)
    {
        if (error)
        {
            ++owner->phases[0]->callback_exceptions;
            owner->phases[0]->record_failure(operation, error);
        }
    };
    socket.set_option(asio::socket_base::send_buffer_size(udp_send_buffer), error);
    failure("UDP SO_SNDBUF configuration: ");
    socket.set_option(asio::socket_base::receive_buffer_size(udp_receive_buffer), error);
    failure("UDP SO_RCVBUF configuration: ");
    asio::socket_base::send_buffer_size send;
    socket.get_option(send, error);
    failure("UDP SO_SNDBUF query: ");
    asio::socket_base::receive_buffer_size receive;
    socket.get_option(receive, error);
    failure("UDP SO_RCVBUF query: ");
    return {send.value(), receive.value()};
}

// A rejection may invoke its completion before async_send returns its acceptance result.
struct send_completion
{
    send_completion(std::shared_ptr<metrics> state, bool server_side, std::size_t expected, bool wire_bytes = false)
        : state(std::move(state)), server_side(server_side), wire_bytes(wire_bytes), expected(expected)
    { ++this->state->pending_completions; }
    std::shared_ptr<metrics> state;
    bool server_side;
    bool wire_bytes;
    std::size_t expected;
    std::mutex mutex;
    bool known{}, accepted{}, completed{}, failed{}, accounted{};

    void account() noexcept
    {
        if (!known || !completed || accounted) return;
        accounted = true;
        if (!accepted)
            ++(server_side ? state->server_rejections : state->client_rejections);
        else if (failed)
            ++(server_side ? state->server_completion_errors : state->client_completion_errors);
        if (state->pending_completions.fetch_sub(1) == 1) state->notify();
    }
    void acceptance(bool value) noexcept
    {
        std::lock_guard lock(mutex);
        known = true;
        accepted = value;
        account();
    }
    void completion(const arknet::error_code& error, std::size_t bytes) noexcept
    {
        std::lock_guard lock(mutex);
        if (completed)
        {
            ++state->duplicate_completions;
            return;
        }
        completed = true;
        failed = static_cast<bool>(error) || (wire_bytes ? bytes < expected : bytes != expected);
        if (failed) state->record_failure(server_side ? "server send: " : "client send: ", error, bytes, expected);
        account();
    }
};

template<class Sender>
bool owned_send(Sender& sender, const std::shared_ptr<const std::string>& payload,
    const std::shared_ptr<metrics>& state, bool server_side)
{
    constexpr bool http_sender = requires { typename Sender::recv_message_type; };
    auto completion = std::make_shared<send_completion>(state, server_side, payload->size(), http_sender);
    try
    {
        auto data = [&]
        {
            if constexpr (http_sender)
            {
                if constexpr (Sender::is_client())
                {
                    http::request<http::string_body> request(http::verb::post, "/echo", 11);
                    request.set(http::field::host, "127.0.0.1");
                    request.body() = *payload;
                    request.prepare_payload();
                    return request;
                }
                else
                {
                    http::response<http::string_body> response(http::status::ok, 11);
                    response.body() = *payload;
                    response.prepare_payload();
                    return response;
                }
            }
            else return *payload;
        }();
        const bool accepted = sender.async_send(std::move(data),
            [completion, payload](const arknet::error_code& error, std::size_t bytes) noexcept
            { completion->completion(error, bytes); });
        completion->acceptance(accepted);
        return accepted;
    }
    catch (...)
    {
        ++state->callback_exceptions;
        completion->acceptance(false);
        completion->completion(asio::error::operation_aborted, 0);
        return false;
    }
}

void put_integer(std::string& message, std::size_t offset, std::uint64_t value, std::size_t bytes)
{
    for (std::size_t i = 0; i < bytes; ++i)
        message[offset + i] = static_cast<char>((value >> ((bytes - 1 - i) * 8)) & 255);
}

std::uint64_t get_integer(std::string_view message, std::size_t offset, std::size_t bytes)
{
    std::uint64_t result{};
    for (std::size_t i = 0; i < bytes; ++i)
        result = (result << 8) | static_cast<unsigned char>(message[offset + i]);
    return result;
}

template<class Client>
struct client_state
{
    struct pending
    {
        std::shared_ptr<const std::string> payload;
        clock_type::time_point sent;
        std::uint64_t sequence{};
    };
    client_state(Client& client, std::shared_ptr<context> owner, const options& settings, std::uint32_t id)
        : client(client), owner(std::move(owner)), settings(settings), id(id), slots(settings.window) {}
    Client& client;
    std::shared_ptr<context> owner;
    const options& settings;
    std::uint32_t id;
    std::vector<pending> slots;
    std::shared_ptr<metrics> phase;
    std::uint64_t next_sequence{}, expected_sequence{}, last_sequence{};
    std::size_t inflight{};
    bool have_sequence{}, stopped_sending{}, finished{};

    void finish(bool force = false) noexcept
    {
        if (!phase || finished) return;
        if (force)
        {
            if (settings.protocol == "udp") phase->udp_lost += inflight;
            else phase->unfinished += inflight;
            for (auto& slot : slots) slot.payload.reset();
            inflight = 0;
        }
        if (inflight) return;
        finished = true;
        if (phase->remaining.fetch_sub(1) == 1) phase->notify();
    }

    void send(std::size_t index)
    {
        if (stopped_sending || clock_type::now() >= phase->deadline) return;
        auto payload = std::make_shared<std::string>(settings.payload, '\0');
        (*payload)[0] = 'A'; (*payload)[1] = 'N'; (*payload)[2] = 'B';
        (*payload)[3] = static_cast<char>(phase->tag);
        const auto sequence = next_sequence++;
        put_integer(*payload, 4, id, 4);
        put_integer(*payload, 8, sequence, 8);
        for (std::size_t i = 16; i < payload->size(); ++i)
            (*payload)[i] = static_cast<char>((sequence + i * 17 + std::uint64_t(id) * 31) & 255);
        auto& slot = slots[index];
        slot.sequence = sequence;
        slot.payload = payload;
        slot.sent = clock_type::now();
        ++inflight;
        if (owned_send(client, slot.payload, phase, false)) ++phase->sent;
        else
        {
            slot.payload.reset();
            --inflight;
            stopped_sending = true;
        }
    }

    void begin(const std::shared_ptr<metrics>& state) noexcept
    {
        phase = state;
        stopped_sending = false;
        finished = false;
        have_sequence = false;
        inflight = 0;
        expected_sequence = next_sequence;
        try
        {
            if (!client.is_started())
            {
                ++phase->disconnects;
                stopped_sending = true;
                finish(true);
                return;
            }
            for (std::size_t i = 0; i < slots.size(); ++i) send(i);
        }
        catch (...)
        {
            ++phase->callback_exceptions;
            stopped_sending = true;
            finish(true);
        }
    }

    void receive(std::string_view message) noexcept
    {
        try
        {
            if (message.size() != settings.payload || message.substr(0, 3) != "ANB" ||
                static_cast<unsigned char>(message[3]) > 1 || get_integer(message, 4, 4) != id)
            {
                ++(phase ? phase : owner->phases[0])->content_errors;
                return;
            }
            const auto incoming_phase = owner->phases[static_cast<unsigned char>(message[3])];
            if (!phase || incoming_phase != phase || finished)
            {
                ++incoming_phase->unexpected;
                return;
            }
            const auto sequence = get_integer(message, 8, 8);
            auto found = std::find_if(slots.begin(), slots.end(), [sequence](const pending& slot)
                { return slot.payload && slot.sequence == sequence; });
            if (found == slots.end())
            {
                ++phase->unexpected;
                return;
            }
            const auto now = clock_type::now();
            if (message != *found->payload) ++phase->content_errors;
            else
            {
                if (settings.protocol == "udp")
                {
                    if (have_sequence && sequence <= last_sequence) ++phase->order_errors;
                    last_sequence = (std::max)(last_sequence, sequence);
                }
                else if (sequence != expected_sequence) ++phase->order_errors;
                expected_sequence = sequence + 1;
                have_sequence = true;
                ++phase->messages;
                phase->observe(std::chrono::duration<double, std::micro>(now - found->sent).count());
            }
            const auto index = static_cast<std::size_t>(found - slots.begin());
            found->payload.reset();
            --inflight;
            if (now < phase->deadline) send(index);
            else finish();
        }
        catch (...)
        {
            ++(phase ? phase : owner->phases[0])->callback_exceptions;
            stopped_sending = true;
        }
    }

    void disconnect() noexcept
    {
        if (!owner->closing && phase)
        {
            ++phase->disconnects;
            stopped_sending = true;
            finish(true);
        }
    }
};

template<class State>
void run_phase(const std::vector<std::shared_ptr<State>>& clients, const std::shared_ptr<metrics>& phase,
    double seconds)
{
    if (seconds == 0) return;
    phase->remaining = clients.size();
    phase->started = clock_type::now();
    phase->deadline = phase->started + std::chrono::duration_cast<clock_type::duration>(
        std::chrono::duration<double>(seconds));
    const auto cpu_start = process_cpu();
    for (auto& state : clients)
        state->client.post([state, phase]() noexcept { state->begin(phase); });
    std::this_thread::sleep_until(phase->deadline);
    for (auto& state : clients)
        state->client.post([state]() noexcept { state->stopped_sending = true; state->finish(); });
    {
        std::unique_lock lock(phase->done_mutex);
        if (!phase->done.wait_until(lock, phase->deadline + drain_time, [&]
            { return phase->remaining == 0 && phase->pending_completions == 0; }))
            ++phase->drain_timeouts;
    }
    std::vector<std::future<void>> barriers;
    for (auto& state : clients)
        barriers.push_back(state->client.post([state]() noexcept
            { state->stopped_sending = true; state->finish(true); }, asio::use_future));
    for (auto& barrier : barriers)
    {
        if (barrier.wait_for(std::chrono::seconds(5)) != std::future_status::ready)
            throw std::runtime_error("client IO barrier timed out");
        barrier.get();
    }
    phase->elapsed = std::chrono::duration<double>(clock_type::now() - phase->started).count();
    const auto cpu_end = process_cpu();
    if (cpu_start && cpu_end) phase->cpu = *cpu_end - *cpu_start;
}

template<bool Framed, bool WebSocket, bool Secure, class Server, class Client, bool Http = false>
void benchmark(const options& settings, const std::shared_ptr<context>& owner)
{
    std::unique_ptr<benchmark_runtime> runtime;
    if (settings.io_model != "owned")
        runtime = std::make_unique<benchmark_runtime>(settings.io_model, settings.io_threads);
    auto server_ptr = runtime ? std::make_unique<Server>(1024, 128 * 1024, runtime->server_lanes(settings.clients)) :
        std::make_unique<Server>(1024, 128 * 1024, 1);
    auto& server = *server_ptr;
    std::vector<std::unique_ptr<Client>> clients;
    std::vector<std::shared_ptr<client_state<Client>>> states;
    clients.reserve(settings.clients);
    states.reserve(settings.clients);
    if constexpr (!Framed && !WebSocket && !Http)
    {
        owner->client_buffers.resize(settings.clients);
        server.bind_init([&server, owner]() noexcept
            { owner->server_buffers = configure_udp(server.acceptor(), owner); });
    }
    if constexpr (Secure)
    {
        server.set_cert_file("", settings.certs + "/server.pem", settings.certs + "/server-key.pem", "");
        if (arknet::get_last_error()) throw std::runtime_error("server certificate loading failed");
    }
    if constexpr (Framed || WebSocket || Http)
        server.bind_connect([owner](auto& session) noexcept
        {
            session->set_no_delay(true);
            if (arknet::get_last_error()) ++owner->phases[0]->callback_exceptions;
        });
    server.bind_recv([owner, work = settings.work](auto& session, const auto& incoming) noexcept
    {
        const std::string_view message = [&]() -> std::string_view
        {
            if constexpr (Http) return incoming.body();
            else return incoming;
        }();
        const auto tag = message.size() >= 16 ? static_cast<unsigned char>(message[3]) : 2;
        const auto state = owner->phases[tag < 2 ? tag : 0];
        try
        {
            if (tag > 1 || message.substr(0, 3) != "ANB")
            {
                ++state->content_errors;
                return;
            }
            if constexpr (WebSocket) session->ws_stream().binary(true);
            benchmark_work(work, message);
            auto payload = std::make_shared<const std::string>(message);
            owned_send(*session, payload, state, true);
        }
        catch (...) { ++state->callback_exceptions; }
    });
    const bool listening = [&]
    {
        if constexpr (Framed) return server.start("127.0.0.1", 0, arknet::use_dgram);
        else return server.start("127.0.0.1", 0);
    }();
    if (!listening) throw std::runtime_error("server start failed: " + arknet::get_last_error().message());
    try
    {
        for (std::size_t i = 0; i < settings.clients; ++i)
        {
            auto client = runtime ? std::make_unique<Client>(1024, 128 * 1024, runtime->context_at(i)) :
                std::make_unique<Client>(1024, 128 * 1024, 1);
            auto state = std::make_shared<client_state<Client>>(*client, owner, settings,
                static_cast<std::uint32_t>(i));
            client->set_auto_reconnect(false);
            client->set_connect_timeout(std::chrono::seconds(10));
            if constexpr (!Framed && !WebSocket && !Http)
                client->bind_init([state]() noexcept
                    { state->owner->client_buffers[state->id] = configure_udp(state->client.socket(), state->owner); });
            if constexpr (Secure) client->load_verify_file(settings.certs + "/ca.pem");
            if constexpr (Framed || WebSocket || Http)
                client->bind_connect([state]() noexcept
                {
                    state->client.set_no_delay(true);
                    if (arknet::get_last_error()) ++state->owner->phases[0]->callback_exceptions;
                    if constexpr (WebSocket) state->client.ws_stream().binary(true);
                });
            client->bind_recv([state](const auto& incoming) noexcept
            {
                if constexpr (Http) state->receive(incoming.body());
                else state->receive(incoming);
            });
            client->bind_disconnect([state]() noexcept { state->disconnect(); });
            const bool connected = [&]
            {
                if constexpr (Framed)
                    return client->start("127.0.0.1", server.get_listen_port(), arknet::use_dgram);
                else if constexpr (WebSocket)
                    return client->start("127.0.0.1", server.get_listen_port(), "/echo");
                else return client->start("127.0.0.1", server.get_listen_port());
            }();
            if (!connected) throw std::runtime_error("client start failed: " + arknet::get_last_error().message());
            clients.push_back(std::move(client));
            states.push_back(std::move(state));
        }
        run_phase(states, owner->phases[0], settings.warmup);
        run_phase(states, owner->phases[1], settings.seconds);
    }
    catch (...)
    {
        owner->closing = true;
        for (auto& client : clients) client->stop();
        server.stop();
        throw;
    }
    owner->closing = true;
    for (auto& client : clients) client->stop();
    server.stop();
}

std::string quoted(std::string_view value)
{
    std::ostringstream stream;
    stream << '"';
    for (const unsigned char c : value)
    {
        if (c == '"' || c == '\\') stream << '\\' << static_cast<char>(c);
        else if (c < 32) stream << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
        else stream << static_cast<char>(c);
    }
    stream << '"';
    return stream.str();
}

bool output(const options& settings, const std::shared_ptr<context>& owner, const std::string& error)
{
    const auto state = owner->phases[1];
    auto samples = state->samples;
    std::sort(samples.begin(), samples.end());
    const auto percentile = [&](double fraction)
    {
        if (samples.empty()) return 0.0;
        return samples[static_cast<std::size_t>(std::ceil(fraction * samples.size())) - 1];
    };
    const auto messages = state->messages.load();
    std::array<int, 2> minimum_buffers{}, maximum_buffers{};
    if (!owner->client_buffers.empty())
    {
        minimum_buffers = maximum_buffers = owner->client_buffers.front();
        for (const auto& buffers : owner->client_buffers)
            for (std::size_t i = 0; i < buffers.size(); ++i)
            {
                minimum_buffers[i] = (std::min)(minimum_buffers[i], buffers[i]);
                maximum_buffers[i] = (std::max)(maximum_buffers[i], buffers[i]);
            }
    }
    const auto udp_value = [&](int value)
        { return settings.protocol == "udp" ? std::to_string(value) : "null"; };
    const std::uint64_t roundtrip_bytes = std::uint64_t(messages) * settings.payload * 2;
    const bool warmup_ok = owner->phases[0]->errors() == 0;
    const bool ok = error.empty() && warmup_ok && state->errors() == 0 && messages > 0 &&
        state->pending_completions == 0;
    std::cout.imbue(std::locale::classic());
    std::cout << std::setprecision(12) << "{\"schema_version\":1,\"backend\":";
#ifdef ARKNET_USE_BOOST_ASIO
    std::cout << "\"boost\"";
#else
    std::cout << "\"standalone\"";
#endif
    std::cout << ",\"protocol\":" << quoted(settings.protocol)
        << ",\"payload_bytes\":" << settings.payload << ",\"clients\":" << settings.clients
        << ",\"window\":" << settings.window
        << ",\"io_model\":" << quoted(settings.io_model)
        << ",\"handler_work\":" << settings.work
        << ",\"io_contexts\":" << (settings.io_model == "owned" ? settings.clients + 1 :
            settings.io_model == "shared" ? 1 : settings.io_threads)
        << ",\"io_threads\":" << (settings.io_model == "owned" ? settings.clients + 1 : settings.io_threads)
        << ",\"server_io_lanes\":" << (settings.io_model == "owned" ? 1 : settings.clients + 1)
        << ",\"requested_seconds\":" << settings.seconds << ",\"warmup_seconds\":" << settings.warmup
        << ",\"elapsed_seconds\":" << state->elapsed << ",\"messages_sent\":" << state->sent
        << ",\"messages\":" << messages << ",\"payload_roundtrip_bytes\":" << roundtrip_bytes
        << ",\"roundtrips_per_second\":" << (state->elapsed > 0 ? messages / state->elapsed : 0)
        << ",\"payload_mib_per_second\":" << (state->elapsed > 0 ? roundtrip_bytes / state->elapsed / 1048576 : 0)
        << ",\"rtt_samples\":" << samples.size() << ",\"rtt_observations\":" << state->observations
        << ",\"rtt_sample_limit\":" << sample_limit << ",\"rtt_p50_us\":" << percentile(0.50)
        << ",\"rtt_p95_us\":" << percentile(0.95) << ",\"rtt_p99_us\":" << percentile(0.99)
        << ",\"client_send_rejections\":" << state->client_rejections
        << ",\"client_send_completion_errors\":" << state->client_completion_errors
        << ",\"server_send_rejections\":" << state->server_rejections
        << ",\"server_send_completion_errors\":" << state->server_completion_errors
        << ",\"content_errors\":" << state->content_errors << ",\"order_errors\":" << state->order_errors
        << ",\"unexpected_messages\":" << state->unexpected << ",\"disconnect_errors\":" << state->disconnects
        << ",\"udp_lost_messages\":" << state->udp_lost << ",\"unfinished_messages\":" << state->unfinished
        << ",\"callback_exceptions\":" << state->callback_exceptions
        << ",\"drain_timeouts\":" << state->drain_timeouts
        << ",\"duplicate_completions\":" << state->duplicate_completions
        << ",\"pending_completions\":" << state->pending_completions
        << ",\"warmup_ok\":" << (warmup_ok ? "true" : "false")
        << ",\"warmup_errors\":" << owner->phases[0]->errors()
        << ",\"udp_send_buffer_requested_bytes\":" << udp_value(udp_send_buffer)
        << ",\"udp_receive_buffer_requested_bytes\":" << udp_value(udp_receive_buffer)
        << ",\"udp_server_send_buffer_bytes\":" << udp_value(owner->server_buffers[0])
        << ",\"udp_server_receive_buffer_bytes\":" << udp_value(owner->server_buffers[1])
        << ",\"udp_client_send_buffer_min_bytes\":" << udp_value(minimum_buffers[0])
        << ",\"udp_client_send_buffer_max_bytes\":" << udp_value(maximum_buffers[0])
        << ",\"udp_client_receive_buffer_min_bytes\":" << udp_value(minimum_buffers[1])
        << ",\"udp_client_receive_buffer_max_bytes\":" << udp_value(maximum_buffers[1])
        << ",\"cpu_seconds\":";
    if (state->cpu) std::cout << *state->cpu; else std::cout << "null";
    std::cout << ",\"peak_rss_bytes\":";
    if (const auto rss = peak_rss()) std::cout << *rss; else std::cout << "null";
    std::cout << ",\"ok\":" << (ok ? "true" : "false") << ",\"error\":";
    if (!error.empty()) std::cout << quoted(error);
    else if (!state->first_failure.empty()) std::cout << quoted(state->first_failure);
    else if (!owner->phases[0]->first_failure.empty())
        std::cout << quoted("warmup " + owner->phases[0]->first_failure);
    else if (!ok) std::cout << "\"benchmark validation failed\"";
    else std::cout << "null";
    std::cout << "}\n";
    return ok;
}
}

int main(int argc, char** argv)
{
    options settings;
    auto owner = std::make_shared<context>();
    std::string error;
    try
    {
        settings = arguments(argc, argv);
        if (settings.protocol == "tcp")
            benchmark<true, false, false, arknet::tcp_server, arknet::tcp_client>(settings, owner);
        else if (settings.protocol == "udp")
            benchmark<false, false, false, arknet::udp_server, arknet::udp_client>(settings, owner);
        else if (settings.protocol == "websocket")
            benchmark<false, true, false, arknet::ws_server, arknet::ws_client>(settings, owner);
        else if (settings.protocol == "http")
            benchmark<false, false, false, arknet::http_server, arknet::http_client, true>(settings, owner);
#ifdef ARKNET_ENABLE_SSL
        else if (settings.protocol == "tcps")
            benchmark<true, false, true, arknet::tcps_server, arknet::tcps_client>(settings, owner);
        else if (settings.protocol == "wss")
            benchmark<false, true, true, arknet::wss_server, arknet::wss_client>(settings, owner);
        else if (settings.protocol == "https")
            benchmark<false, false, true, arknet::https_server, arknet::https_client, true>(settings, owner);
#endif
        else throw std::invalid_argument("TLS support is disabled in this build");
    }
    catch (const std::exception& failure) { error = failure.what(); }
    catch (...) { error = "unknown benchmark failure"; }
    return output(settings, owner, error) ? 0 : 1;
}
