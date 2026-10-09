#include <arknet/arknet.hpp>
#include "check.hpp"
#include "../benchmarks/runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>

namespace
{
enum class protocol
{
    tcp,
    udp,
    websocket,
    http
};

struct options
{
    std::string selected = "all";
#if defined(ARKNET_TEST_CERT_DIR)
    std::string certs = ARKNET_TEST_CERT_DIR;
#else
    std::string certs;
#endif
    std::size_t clients = 16;
    std::size_t rounds = 2;
    std::size_t window = 4;
    std::size_t messages = 64;
    std::size_t seconds = 0;
    std::string test_case;
    bool list_test_cases = false;
};

options stress_config;

std::size_t number(std::string_view value)
{
    std::size_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::invalid_argument("invalid stress number");
    return result;
}

options parse(int argc, char** argv)
{
    options result;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view name(argv[i]);
        if (name == "--list-test-cases")
        {
            result.list_test_cases = true;
            continue;
        }
        if (name.starts_with("--test-case="))
        {
            result.test_case = name.substr(12);
            continue;
        }
        if (i + 1 >= argc)
            throw std::invalid_argument("stress option requires a value");
        const std::string_view value(argv[++i]);
        if (name == "--protocol")
            result.selected = value;
        else if (name == "--certs")
            result.certs = value;
        else if (name == "--clients")
            result.clients = number(value);
        else if (name == "--rounds")
            result.rounds = number(value);
        else if (name == "--window")
            result.window = number(value);
        else if (name == "--messages")
            result.messages = number(value);
        else if (name == "--seconds")
            result.seconds = number(value);
        else if (name == "--test-case")
            result.test_case = value;
        else
            throw std::invalid_argument("unknown stress option");
    }
    if (result.clients < 16 || result.clients > 64)
        throw std::invalid_argument("stress clients must be between 16 and 64");
    if (result.rounds < 2 || result.rounds > 10000)
        throw std::invalid_argument("stress rounds must be between 2 and 10000");
    if (result.window == 0 || result.window > 16)
        throw std::invalid_argument("stress window must be between 1 and 16");
    if (result.messages < result.window || result.messages > 4096)
        throw std::invalid_argument("invalid stress messages");
    if (result.seconds > 3600)
        throw std::invalid_argument("stress seconds exceeds one hour");
    bool selected = result.selected == "all" || result.selected == "tcp" || result.selected == "udp" ||
                    result.selected == "ws" || result.selected == "http";
#if defined(ARKNET_ENABLE_SSL)
    selected = selected || result.selected == "tcps" || result.selected == "wss" || result.selected == "https";
#endif
    if (!selected)
        throw std::invalid_argument("unknown or disabled stress protocol");
    return result;
}

struct io_runner
{
    asio::io_context context;
    asio::executor_work_guard<asio::io_context::executor_type> work{context.get_executor()};
    std::thread thread{[this] { context.run(); }};
    ~io_runner()
    {
        work.reset();
        context.stop();
        thread.join();
    }
};

struct cleanup
{
    std::function<void()> stop;
    ~cleanup()
    {
        if (stop)
            stop();
    }
};

bool accept_peer(asio::ip::tcp::acceptor& listener, asio::ip::tcp::socket& peer, const std::atomic<bool>& abort)
{
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    arknet::error_code ec;
    while (!abort && std::chrono::steady_clock::now() < deadline)
    {
        listener.accept(peer, ec);
        if (!ec)
            return true;
        if (ec != asio::error::would_block && ec != asio::error::try_again)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return false;
}

bool read_peer(asio::ip::tcp::socket& peer, std::string& data, const std::atomic<bool>& abort)
{
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    arknet::error_code ec;
    peer.non_blocking(true, ec);
    if (ec)
        return false;
    std::size_t offset = 0;
    while (!abort && offset < data.size() && std::chrono::steady_clock::now() < deadline)
    {
        offset += peer.read_some(asio::buffer(data.data() + offset, data.size() - offset), ec);
        if (ec && ec != asio::error::would_block && ec != asio::error::try_again)
            return false;
        if (ec)
            std::this_thread::sleep_for(1ms);
    }
    return offset == data.size();
}

std::string packet(std::uint32_t round, std::uint32_t client, std::uint32_t sequence)
{
    const std::array<std::uint32_t, 3> header{round, client, sequence};
    std::string data(sizeof(header) + 32 + sequence % 8 * 113, '\0');
    std::memcpy(data.data(), header.data(), sizeof(header));
    for (std::size_t i = sizeof(header); i < data.size(); ++i)
        data[i] = static_cast<char>((round * 7 + client * 19 + sequence * 31 + i) % 251);
    return data;
}

bool header(std::string_view data, std::array<std::uint32_t, 3>& value)
{
    if (data.size() < sizeof(value))
        return false;
    std::memcpy(value.data(), data.data(), sizeof(value));
    return true;
}

struct batch
{
    explicit batch(std::size_t size, std::uint32_t generation)
        : echo_calls(size), seen(size), total(size), round(generation)
    {
    }
    std::vector<std::atomic<unsigned>> echo_calls;
    std::vector<std::atomic<unsigned>> seen;
    const std::size_t total;
    const std::uint32_t round;
    std::atomic<std::size_t> completed{};
    std::atomic<std::size_t> received{};
    std::atomic<bool> failed{};
    std::mutex mutex;
    std::condition_variable changed;

    void notify()
    {
        std::lock_guard lock(mutex);
        changed.notify_all();
    }
    void fail()
    {
        failed = true;
        notify();
    }
    bool wait()
    {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 20s, [&] { return failed || (completed == total * 2 && received == total); }) &&
               !failed;
    }
};

template <protocol Kind, class Sender, class Data, class Callback>
bool stress_send(Sender& sender, Data&& data, Callback&& callback)
{
    if constexpr (Kind == protocol::http)
    {
        if constexpr (Sender::is_client())
        {
            http::request<http::string_body> request(http::verb::post, "/stress", 11);
            request.set(http::field::host, "localhost");
            request.body() = std::forward<Data>(data);
            request.prepare_payload();
            return sender.async_send(std::move(request), std::forward<Callback>(callback));
        }
        else
        {
            http::response<http::string_body> response(http::status::ok, 11);
            response.body() = std::forward<Data>(data);
            response.prepare_payload();
            return sender.async_send(std::move(response), std::forward<Callback>(callback));
        }
    }
    else
        return sender.async_send(std::forward<Data>(data), std::forward<Callback>(callback));
}

template <class Client, protocol Kind> struct flow
{
    flow(std::shared_ptr<batch> owner, std::size_t identity, std::size_t count, std::size_t pipeline,
         std::unique_ptr<Client> client)
        : monitor(std::move(owner)), id(identity), total(count), window(pipeline), calls(count), seen(count),
          peer(std::move(client))
    {
    }
    std::shared_ptr<batch> monitor;
    const std::size_t id;
    const std::size_t total;
    const std::size_t window;
    std::vector<std::atomic<unsigned>> calls;
    std::vector<bool> seen;
    std::size_t next = 0;
    std::size_t received = 0;
    std::unique_ptr<Client> peer;

    void begin(std::shared_ptr<batch> owner)
    {
        monitor = std::move(owner);
        for (auto& count : calls)
            count = 0;
        std::fill(seen.begin(), seen.end(), false);
        next = received = 0;
        pump();
    }

    void pump()
    {
        while (!monitor->failed && next < total && next - received < window)
        {
            const auto sequence = next++;
            auto data = packet(monitor->round, static_cast<std::uint32_t>(id), static_cast<std::uint32_t>(sequence));
            const auto bytes = data.size();
            const bool accepted = stress_send<Kind>(
                *peer, std::move(data),
                [this, current = monitor, sequence, bytes](const arknet::error_code& ec, std::size_t count)
                {
                    if (calls[sequence].fetch_add(1) != 0 || ec ||
                        (Kind == protocol::http ? count <= bytes : count != bytes))
                        current->fail();
                    ++current->completed;
                    current->notify();
                });
            if (!accepted)
                monitor->fail();
        }
    }

    void receive(std::string_view data)
    {
        std::array<std::uint32_t, 3> value{};
        if (!header(data, value) || value[0] != monitor->round || value[1] != id || value[2] >= total ||
            seen[value[2]] || data != packet(value[0], value[1], value[2]) ||
            (Kind != protocol::udp && value[2] != received))
        {
            monitor->fail();
            return;
        }
        seen[value[2]] = true;
        ++received;
        ++monitor->received;
        monitor->notify();
        pump();
    }
};

template <protocol Kind, class Server> bool listen(Server& server)
{
    if constexpr (Kind == protocol::tcp)
        return server.start("127.0.0.1", 0, arknet::use_dgram);
    else
        return server.start("127.0.0.1", 0);
}

template <protocol Kind, class Client> bool connect(Client& client, std::uint16_t port)
{
    const char* host = "127.0.0.1";
    if constexpr (Kind == protocol::tcp)
        return client.start(host, port, arknet::use_dgram);
    else if constexpr (Kind == protocol::websocket)
        return client.start(host, port, "/stress");
    else
        return client.start(host, port);
}

template <class Server, class Client, protocol Kind>
void load(const options& config, const char* name, std::string_view model, std::size_t threads)
{
    const bool external = !model.empty();
    const std::string label = external ? " " + std::string(model) + "-" + std::to_string(threads) : " owned";
    std::unique_ptr<benchmark_runtime> runner =
        external ? std::make_unique<benchmark_runtime>(model, threads) : nullptr;
    std::shared_ptr<batch> active;
    auto server = external ? std::make_unique<Server>(2048, 1024 * 1024, runner->server_lanes(config.clients))
                           : std::make_unique<Server>(2048, 1024 * 1024, 4);
    if constexpr (requires { server->set_cert_file("", "", "", ""); })
    {
        check(!config.certs.empty(), "TLS stress requires --certs");
        server->set_cert_file("", config.certs + "/server.pem", config.certs + "/server-key.pem", "");
        check(!arknet::get_last_error(), "stress server identity");
    }
    server->bind_recv(
        [&](auto& session, const auto& incoming)
        {
            const std::string_view data = [&]() -> std::string_view
            {
                if constexpr (Kind == protocol::http)
                    return incoming.body();
                else
                    return incoming;
            }();
            auto current = std::atomic_load(&active);
            std::array<std::uint32_t, 3> value{};
            if (!header(data, value) || value[0] != current->round || value[1] >= config.clients ||
                value[2] >= config.messages || data != packet(value[0], value[1], value[2]))
            {
                current->fail();
                return;
            }
            const auto index = value[1] * config.messages + value[2];
            if (current->seen[index].fetch_add(1) != 0)
                current->fail();
            if constexpr (Kind == protocol::websocket)
                session->ws_stream().binary(true);
            const auto bytes = data.size();
            if (!stress_send<Kind>(*session, std::string(data),
                                   [current, index, bytes](const arknet::error_code& ec, std::size_t count)
                                   {
                                       if (current->echo_calls[index].fetch_add(1) != 0 || ec ||
                                           (Kind == protocol::http ? count <= bytes : count != bytes))
                                           current->fail();
                                       ++current->completed;
                                       current->notify();
                                   }))
                current->fail();
        });

    std::vector<std::unique_ptr<Client>> clients;
    for (std::size_t i = 0; i < config.clients; ++i)
    {
        auto client = external ? std::make_unique<Client>(2048, 1024 * 1024, runner->context_at(i))
                               : std::make_unique<Client>(2048, 1024 * 1024, 1);
        client->set_auto_reconnect(false);
        client->set_connect_timeout(10s);
        client->set_max_send_buffer_size(config.window * 2048);
        if constexpr (requires { client->load_verify_file(std::string{}); })
            client->load_verify_file(config.certs + "/ca.pem");
        if constexpr (Kind == protocol::websocket)
            client->bind_connect([ptr = client.get()] { ptr->ws_stream().binary(true); });
        clients.push_back(std::move(client));
    }

    const auto duration = std::chrono::milliseconds(config.seconds * 250) / config.rounds;
    std::size_t batches = 0;
    for (std::size_t round = 0; round < config.rounds; ++round)
    {
        auto monitor = std::make_shared<batch>(config.clients * config.messages, static_cast<std::uint32_t>(batches));
        std::atomic_store(&active, monitor);
        std::vector<std::unique_ptr<flow<Client, Kind>>> runs;
        for (std::size_t i = 0; i < config.clients; ++i)
            runs.push_back(std::make_unique<flow<Client, Kind>>(monitor, i, config.messages, config.window,
                                                                std::move(clients[i])));
        cleanup stop_all{[&]
                         {
                             for (auto& run : runs)
                                 run->peer->request_stop();
                             for (auto& run : runs)
                                 run->peer->wait_stopped();
                             server->request_stop();
                             server->wait_stopped();
                         }};
        if (!listen<Kind>(*server))
        {
            const auto ec = arknet::get_last_error();
            std::cerr << name << label << " listen round=" << round << ": " << ec.category().name() << ':' << ec.value()
                      << " " << ec.message() << std::endl;
            check(false, "stress server start/restart");
        }
        for (auto& run : runs)
        {
            run->peer->bind_recv(
                [ptr = run.get()](const auto& data)
                {
                    if constexpr (Kind == protocol::http)
                        ptr->receive(data.body());
                    else
                        ptr->receive(data);
                });
            if (!connect<Kind>(*run->peer, server->get_listen_port()))
            {
                const auto ec = arknet::get_last_error();
                std::cerr << name << label << " connect round=" << round << " client=" << run->id
                          << " port=" << server->get_listen_port() << ": " << ec.category().name() << ':' << ec.value()
                          << " " << ec.message() << std::endl;
                check(false, "stress client connect/reconnect");
            }
        }
        const auto deadline = std::chrono::steady_clock::now() + duration;
        do
        {
            std::atomic_store(&active, monitor);
            for (auto& run : runs)
                run->peer->post([ptr = run.get(), monitor] { ptr->begin(monitor); });
            const bool finished = monitor->wait();
            if (!finished)
            {
                std::cerr << name << label << " batch round=" << round << " generation=" << batches
                          << " received=" << monitor->received << '/' << monitor->total
                          << " completions=" << monitor->completed << '/' << monitor->total * 2 << std::endl;
                check(false, (monitor->failed ? "stress content/sequence/completion failure" : "stress batch timeout"));
            }
            for (auto& run : runs)
                for (const auto& count : run->calls)
                    check(count == 1, "stress client completion is not exactly once");
            for (std::size_t i = 0; i < monitor->total; ++i)
                check(monitor->echo_calls[i] == 1 && monitor->seen[i] == 1, "stress server echo is not exactly once");
            ++batches;
            if (std::chrono::steady_clock::now() >= deadline)
                break;
            monitor = std::make_shared<batch>(config.clients * config.messages, static_cast<std::uint32_t>(batches));
        } while (true);
        stop_all.stop();
        stop_all.stop = {};
        check(server->get_session_count() == 0, "stress stop retains server sessions");
        for (auto& run : runs)
        {
            check(run->peer->is_stopped() && run->peer->get_queued_send_buffer_size() == 0,
                  "stress stop retains client work");
            clients[run->id] = std::move(run->peer);
        }
        if (runner)
        {
            check(!runner->context_at().stopped(), "stress stops shared context");
            std::promise<void> alive;
            auto future = alive.get_future();
            asio::post(runner->context_at(), [&] { alive.set_value(); });
            await(future);
        }
    }
    std::cout << name << label << ": " << config.clients << " clients, " << config.rounds << " restarts, " << batches
              << " batches, " << batches * config.clients * config.messages << " verified echoes" << std::endl;
}

void slow_receiver()
{
    constexpr std::size_t size = 256 * 1024;
    constexpr std::size_t attempts = 12;
    asio::io_context context;
    asio::ip::tcp::acceptor listener(context, {asio::ip::make_address("127.0.0.1"), 0});
    listener.non_blocking(true);
    asio::ip::tcp::socket socket(context);
    std::promise<void> accepted;
    auto connected = accepted.get_future();
    std::mutex mutex;
    std::condition_variable ready;
    bool release = false;
    std::vector<std::size_t> expected;
    std::atomic<bool> integrity{true};
    std::atomic<bool> abort{};
    std::thread reader(
        [&]
        {
            arknet::error_code ec;
            if (!accept_peer(listener, socket, abort))
            {
                integrity = false;
                accepted.set_value();
                return;
            }
            socket.set_option(asio::socket_base::receive_buffer_size(4096), ec);
            accepted.set_value();
            std::unique_lock lock(mutex);
            if (!ready.wait_for(lock, 20s, [&] { return release; }))
            {
                integrity = false;
                return;
            }
            lock.unlock();
            std::string data(size, '\0');
            for (const auto index : expected)
            {
                if (!read_peer(socket, data, abort) || data != std::string(size, static_cast<char>('a' + index)))
                {
                    integrity = false;
                    break;
                }
            }
        });
    std::array<std::atomic<unsigned>, attempts> calls{};
    std::atomic<std::size_t> completed{};
    std::atomic<std::size_t> rejected{};
    std::atomic<bool> valid{true};
    arknet::tcp_client client;
    cleanup finish{[&]
                   {
                       abort = true;
                       {
                           std::lock_guard lock(mutex);
                           release = true;
                       }
                       ready.notify_all();
                       if (reader.joinable())
                           reader.join();
                       arknet::error_code ec;
                       socket.close(ec);
                       client.stop();
                   }};
    client.set_auto_reconnect(false);
    client.set_max_send_buffer_size(size * 2);
    client.bind_init([&] { client.set_sndbuf_size(4096); });
    check(client.start("127.0.0.1", listener.local_endpoint().port()), "slow receiver connect");
    await(connected);
    for (std::size_t i = 0; i < attempts; ++i)
    {
        const bool queued = client.async_send(std::string(size, static_cast<char>('a' + i)),
                                              [&, i](const arknet::error_code& ec, std::size_t bytes)
                                              {
                                                  if (calls[i].fetch_add(1) != 0)
                                                      valid = false;
                                                  if (ec == asio::error::no_buffer_space && bytes == 0)
                                                      ++rejected;
                                                  else if (ec || bytes != size)
                                                      valid = false;
                                                  ++completed;
                                                  std::lock_guard lock(mutex);
                                                  ready.notify_all();
                                              });
        if (queued)
            expected.push_back(i);
    }
    check(!expected.empty() && expected.size() < attempts, "slow receiver must apply queue backpressure");
    check(client.get_queued_send_buffer_size() <= size * 2, "slow receiver queue exceeds limit");
    {
        std::lock_guard lock(mutex);
        release = true;
    }
    ready.notify_all();
    {
        std::unique_lock lock(mutex);
        check(ready.wait_for(lock, 20s, [&] { return completed == attempts; }), "slow receiver completions timeout");
    }
    reader.join();
    check(valid && integrity && rejected == attempts - expected.size(), "slow receiver content/completion mismatch");
    check(client.get_queued_send_buffer_size() == 0, "slow receiver queue does not drain");
    auto recovery = client.async_send(std::string("recovered"), asio::use_future);
    check(!await(recovery).first, "slow receiver rejects sends after recovery");
    std::string recovered(9, '\0');
    check(read_peer(socket, recovered, abort) && recovered == "recovered", "slow receiver recovery payload mismatch");
    client.stop();
    finish.stop = {};
    for (const auto& count : calls)
        check(count == 1, "slow receiver completion is not exactly once");
    std::cout << "TCP slow receiver: " << expected.size() << " accepted, " << rejected << " rejected, queue drained"
              << std::endl;
}

void blocked_stop()
{
    constexpr std::size_t size = 2 * 1024 * 1024;
    asio::io_context context;
    asio::ip::tcp::acceptor listener(context, {asio::ip::make_address("127.0.0.1"), 0});
    listener.non_blocking(true);
    asio::ip::tcp::socket socket(context);
    std::promise<void> accepted;
    auto connected = accepted.get_future();
    std::mutex mutex;
    std::condition_variable ready;
    bool stopped = false;
    std::atomic<bool> watchdog{};
    std::atomic<bool> abort{};
    std::thread peer(
        [&]
        {
            arknet::error_code ec;
            if (!accept_peer(listener, socket, abort))
            {
                watchdog = true;
                accepted.set_value();
                return;
            }
            socket.set_option(asio::socket_base::receive_buffer_size(4096), ec);
            accepted.set_value();
            std::unique_lock lock(mutex);
            watchdog = !ready.wait_for(lock, 2s, [&] { return stopped; });
            socket.close(ec);
        });
    std::array<std::atomic<unsigned>, 2> calls{};
    std::atomic<bool> valid{true};
    arknet::tcp_client client;
    cleanup finish{[&]
                   {
                       abort = true;
                       {
                           std::lock_guard lock(mutex);
                           stopped = true;
                       }
                       ready.notify_all();
                       if (peer.joinable())
                           peer.join();
                       client.stop();
                   }};
    client.set_auto_reconnect(false);
    client.set_disconnect_timeout(200ms);
    client.bind_init([&] { client.set_sndbuf_size(4096); });
    check(client.start("127.0.0.1", listener.local_endpoint().port()), "blocked stop connect");
    await(connected);
    for (std::size_t i = 0; i < calls.size(); ++i)
        check(client.async_send(std::string(size, 'x'),
                                [&, i](const arknet::error_code&, std::size_t bytes)
                                {
                                    if (calls[i].fetch_add(1) != 0 || bytes > size)
                                        valid = false;
                                }),
              "blocked stop accepts pending sends");
    const auto started = std::chrono::steady_clock::now();
    client.stop();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    {
        std::lock_guard lock(mutex);
        stopped = true;
    }
    ready.notify_all();
    peer.join();
    finish.stop = {};
    std::cout << "TCP blocked writer stop: " << elapsed.count() << " ms, peer watchdog=" << watchdog << std::endl;
    check(!watchdog, "stop waits for a blocked write until the peer watchdog closes it");
    check(valid && calls[0] == 1 && calls[1] == 1 && client.get_queued_send_buffer_size() == 0,
          "blocked stop leaves incomplete sends or queued bytes");
}

void blocked_server_stop(bool external)
{
    constexpr std::size_t size = 2 * 1024 * 1024;
    std::unique_ptr<io_runner> runner = external ? std::make_unique<io_runner>() : nullptr;
    std::array<std::atomic<unsigned>, 2> calls{};
    std::atomic<bool> valid{true};
    std::atomic<bool> notified{};
    std::weak_ptr<arknet::tcp_session> session;
    std::promise<void> sending;
    auto started = sending.get_future();
    auto server = external ? std::make_unique<arknet::tcp_server>(runner->context)
                           : std::make_unique<arknet::tcp_server>(2048, 1024 * 1024, 2);
    server->bind_connect(
        [&](auto& connected)
        {
            if (notified.exchange(true))
            {
                valid = false;
                return;
            }
            session = connected;
            connected->set_disconnect_timeout(200ms);
            connected->set_sndbuf_size(4096);
            for (std::size_t i = 0; i < calls.size(); ++i)
                if (!connected->async_send(std::string(size, 's'),
                                           [&, i](const arknet::error_code&, std::size_t bytes)
                                           {
                                               if (calls[i].fetch_add(1) != 0 || bytes > size)
                                                   valid = false;
                                           }))
                    valid = false;
            sending.set_value();
        });
    check(server->start("127.0.0.1", 0), "blocked server listen");
    asio::io_context context;
    asio::ip::tcp::socket socket(context);
    socket.open(asio::ip::tcp::v4());
    socket.set_option(asio::socket_base::receive_buffer_size(4096));
    socket.connect({asio::ip::make_address("127.0.0.1"), server->get_listen_port()});
    await(started);
    std::mutex mutex;
    std::condition_variable ready;
    bool stopped = false;
    std::atomic<bool> watchdog{};
    std::thread peer(
        [&]
        {
            std::unique_lock lock(mutex);
            watchdog = !ready.wait_for(lock, 2s, [&] { return stopped; });
            arknet::error_code ec;
            socket.close(ec);
        });
    cleanup finish{[&]
                   {
                       {
                           std::lock_guard lock(mutex);
                           stopped = true;
                       }
                       ready.notify_all();
                       if (peer.joinable())
                           peer.join();
                       server->stop();
                   }};
    const auto begun = std::chrono::steady_clock::now();
    server->stop();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begun);
    {
        std::lock_guard lock(mutex);
        stopped = true;
    }
    ready.notify_all();
    peer.join();
    finish.stop = {};
    std::cout << "TCP blocked server " << (external ? "external" : "owned") << ": " << elapsed.count()
              << " ms, peer watchdog=" << watchdog << std::endl;
    check(!watchdog, "server stop waits for blocked session writes until the peer watchdog closes it");
    check(valid && calls[0] == 1 && calls[1] == 1 && server->get_session_count() == 0,
          "blocked server stop leaves sends or sessions");
    if (const auto remaining = session.lock())
        check(remaining->get_queued_send_buffer_size() == 0, "blocked server stop retains session queued bytes");
    if (runner)
        check(!runner->context.stopped(), "blocked server stops host context");
}

void stop_during_connect(bool external)
{
    std::unique_ptr<io_runner> runner = external ? std::make_unique<io_runner>() : nullptr;
    std::atomic<unsigned> connections{};
    std::atomic<unsigned> disconnections{};
    std::promise<void> connected;
    auto notified = connected.get_future();
    auto server = external ? std::make_unique<arknet::tcp_server>(runner->context)
                           : std::make_unique<arknet::tcp_server>(2048, 1024 * 1024, 2);
    auto client =
        external ? std::make_unique<arknet::tcp_client>(runner->context) : std::make_unique<arknet::tcp_client>();
    cleanup finish{[&]
                   {
                       client->stop();
                       server->stop();
                   }};
    client->set_auto_reconnect(false);
    client->set_connect_timeout(2s);
    server->bind_connect(
        [&](auto& session)
        {
            const auto previous = connections.fetch_add(1);
            session->stop();
            if (previous == 0)
                connected.set_value();
        });
    server->bind_disconnect([&](auto&) { ++disconnections; });
    check(server->start("127.0.0.1", 0), "stop-in-connect server listen");
    client->start("127.0.0.1", server->get_listen_port());
    await(notified);
    finish.stop();
    finish.stop = {};
    check(connections == 1 && disconnections == 0 && server->get_session_count() == 0,
          "stop-in-connect leaves a session or emits disconnect for an unregistered session");
    if (runner)
    {
        check(!runner->context.stopped(), "stop-in-connect stops host context");
        std::promise<void> alive;
        auto completed = alive.get_future();
        asio::post(runner->context, [&] { alive.set_value(); });
        await(completed);
    }
    std::cout << "TCP stop in connect " << (external ? "external" : "owned") << ": session cleaned" << std::endl;
}

template <class Server, class Client, protocol Kind> void run_protocol(const options& config, const char* name)
{
    load<Server, Client, Kind>(config, name, "", 1);
    load<Server, Client, Kind>(config, name, "shared", 1);
    load<Server, Client, Kind>(config, name, "shared", 4);
    load<Server, Client, Kind>(config, name, "sharded", 4);
}

DOCTEST_TEST_CASE("stress.tcp.load" * doctest::test_suite("stress.tcp"))
{
    run_protocol<arknet::tcp_server, arknet::tcp_client, protocol::tcp>(stress_config, "TCP");
}

DOCTEST_TEST_CASE("stress.tcp.slow_receiver" * doctest::test_suite("stress.tcp"))
{
    slow_receiver();
}

DOCTEST_TEST_CASE("stress.tcp.blocked_stop" * doctest::test_suite("stress.tcp"))
{
    blocked_stop();
    blocked_server_stop(false);
    blocked_server_stop(true);
}

DOCTEST_TEST_CASE("stress.tcp.stop_during_connect" * doctest::test_suite("stress.tcp"))
{
    stop_during_connect(false);
    stop_during_connect(true);
}

DOCTEST_TEST_CASE("stress.udp.load" * doctest::test_suite("stress.udp"))
{
    run_protocol<arknet::udp_server, arknet::udp_client, protocol::udp>(stress_config, "UDP");
}

DOCTEST_TEST_CASE("stress.ws.load" * doctest::test_suite("stress.ws"))
{
    run_protocol<arknet::ws_server, arknet::ws_client, protocol::websocket>(stress_config, "WS");
}

DOCTEST_TEST_CASE("stress.http.load" * doctest::test_suite("stress.http"))
{
    run_protocol<arknet::http_server, arknet::http_client, protocol::http>(stress_config, "HTTP");
}

#if defined(ARKNET_ENABLE_SSL)
DOCTEST_TEST_CASE("stress.tcps.load" * doctest::test_suite("stress.tcps"))
{
    run_protocol<arknet::tcps_server, arknet::tcps_client, protocol::tcp>(stress_config, "TCPS");
}

DOCTEST_TEST_CASE("stress.wss.load" * doctest::test_suite("stress.wss"))
{
    run_protocol<arknet::wss_server, arknet::wss_client, protocol::websocket>(stress_config, "WSS");
}

DOCTEST_TEST_CASE("stress.https.load" * doctest::test_suite("stress.https"))
{
    run_protocol<arknet::https_server, arknet::https_client, protocol::http>(stress_config, "HTTPS");
}
#endif
}

int main(int argc, char** argv)
{
    std::cout << std::unitbuf;
    try
    {
        stress_config = parse(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
    doctest::Context context;
    if (stress_config.list_test_cases)
    {
        const char* argument = "--list-test-cases";
        context.applyCommandLine(1, &argument);
    }
    if (stress_config.selected != "all")
        context.addFilter("test-suite", ("stress." + stress_config.selected).c_str());
    if (!stress_config.test_case.empty())
        context.addFilter("test-case", stress_config.test_case.c_str());
    return context.run();
}
