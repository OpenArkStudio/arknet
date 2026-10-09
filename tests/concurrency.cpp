#include <arknet/arknet.hpp>
#include "check.hpp"
#include "../benchmarks/runtime.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <charconv>
#include <latch>
#include <memory>
#include <optional>
#include <thread>
#include <set>

namespace
{
constexpr std::size_t sender_count = 8;
constexpr std::size_t payload_size = 256;
constexpr std::size_t operation_limit = 1024;

class watchdog
{
public:
    watchdog()
        : thread_(
              [this]
              {
                  std::unique_lock lock(mutex_);
                  if (!ready_.wait_for(lock, 45s, [this] { return finished_; }))
                  {
                      std::cerr << "concurrent send/stop fixture hung\n";
                      std::_Exit(EXIT_FAILURE);
                  }
              })
    {
    }

    ~watchdog()
    {
        {
            std::lock_guard lock(mutex_);
            finished_ = true;
        }
        ready_.notify_all();
        thread_.join();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    bool finished_{};
    std::thread thread_;
};

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

struct latch_release
{
    std::latch& latch;
    bool released{};
    void open()
    {
        if (!released)
        {
            released = true;
            latch.count_down();
        }
    }
    ~latch_release() { open(); }
};

class io_pause
{
    struct state
    {
        std::promise<void> entered;
        std::promise<void> released;
        std::shared_future<void> release = released.get_future().share();
        std::atomic<bool> open{};
    };

public:
    template <class Client> explicit io_pause(Client& client) : state_(std::make_shared<state>())
    {
        auto entered = state_->entered.get_future();
        client.post(
            [state = state_]
            {
                state->entered.set_value();
                state->release.wait();
            });
        await(entered);
    }

    ~io_pause() { open(); }

    void open()
    {
        if (!state_->open.exchange(true))
            state_->released.set_value();
    }

private:
    std::shared_ptr<state> state_;
};

class outcomes
{
    struct result
    {
        bool submitted{};
        bool accepted{};
        bool after_stop{};
        std::size_t callbacks{};
        std::size_t bytes{};
        arknet::error_code error;
        std::array<std::size_t, 2> received{};
    };

public:
    outcomes(std::size_t attempts, bool empty = false) : payloads_(attempts), results_(attempts)
    {
        for (std::size_t id = 0; id < attempts; ++id)
        {
            if (!empty || id + 1 == attempts)
            {
                payloads_[id] = std::to_string(id) + ':';
                payloads_[id].resize(payload_size, static_cast<char>('A' + id % 26));
            }
        }
    }

    const std::string& payload(std::size_t id) const { return payloads_[id]; }

    void submitted(std::size_t id, bool accepted, bool after_stop = false)
    {
        std::lock_guard lock(mutex_);
        auto& result = results_[id];
        result.submitted = true;
        result.accepted = accepted;
        result.after_stop = after_stop;
    }

    void complete(std::size_t id, const arknet::error_code& error, std::size_t bytes)
    {
        {
            std::lock_guard lock(mutex_);
            auto& result = results_[id];
            ++result.callbacks;
            result.error = error;
            result.bytes = bytes;
            ++completed_;
        }
        ready_.notify_all();
    }

    void receive(std::size_t side, std::string_view payload)
    {
        {
            std::lock_guard lock(mutex_);
            ++received_[side];
            if (payload.empty())
                ++empty_received_[side];
            else
            {
                const auto end = payload.find(':');
                std::size_t id{};
                const auto parsed = std::from_chars(
                    payload.data(), payload.data() + (end == std::string_view::npos ? payload.size() : end), id);
                if (end == std::string_view::npos || parsed.ec != std::errc{} || parsed.ptr != payload.data() + end ||
                    id >= payloads_.size() || payload != payloads_[id])
                    invalid_payload_ = true;
                else
                    ++results_[id].received[side];
            }
        }
        ready_.notify_all();
    }

    void queue_size(std::size_t bytes, std::size_t limit)
    {
        if (bytes > limit)
            queue_overflow_.store(true);
    }

    void submission_threw() { submission_exception_.store(true); }

    std::size_t accepted() const
    {
        std::lock_guard lock(mutex_);
        std::size_t count{};
        for (const auto& result : results_)
            count += result.submitted && result.accepted;
        return count;
    }

    void wait_completions(std::size_t count)
    {
        std::unique_lock lock(mutex_);
        check(ready_.wait_for(lock, 5s, [&] { return completed_ >= count; }), "concurrent completion timeout");
    }

    void wait_delivery(std::size_t count)
    {
        std::unique_lock lock(mutex_);
        check(ready_.wait_for(lock, 5s, [&] { return received_[0] >= count && received_[1] >= count; }),
              "concurrent echo timeout");
    }

    void verify(bool stopping)
    {
        std::lock_guard lock(mutex_);
        check(!invalid_payload_ && !queue_overflow_ && !submission_exception_,
              "invalid data, queue overflow, or send exception");
        std::size_t accepted_empty{};
        for (std::size_t id = 0; id < results_.size(); ++id)
        {
            const auto& result = results_[id];
            const auto size = payloads_[id].size();
            check(result.submitted && result.callbacks == 1, "each submission must complete exactly once");
            if (!result.accepted)
                check((result.error == asio::error::no_buffer_space || result.error == asio::error::not_connected) &&
                          result.bytes == 0,
                      "rejected send error or byte count");
            else if (!result.error)
                check(result.bytes == size, "successful send byte count");
            else
            {
                check(stopping && result.bytes <= size &&
                          (result.error == asio::error::operation_aborted ||
                           result.error == asio::error::not_connected || result.error == asio::error::bad_descriptor ||
                           result.error == asio::error::broken_pipe || result.error == asio::error::connection_reset ||
                           result.error == asio::error::connection_aborted || result.error == asio::error::eof ||
                           result.error == websocket::error::closed),
                      "accepted send cancellation error or byte count");
            }
            if (result.after_stop)
                check(!result.accepted && result.error == asio::error::not_connected,
                      "send after stop must reject as not_connected");
            check(result.received[0] <= 1 && result.received[1] <= result.received[0], "duplicate or unexpected echo");
            if (!result.accepted)
                check(result.received[0] == 0, "rejected payload must never reach the server");
            if (!stopping && result.accepted)
            {
                if (size == 0)
                    ++accepted_empty;
                else
                    check(result.received[0] == 1 && result.received[1] == 1,
                          "accepted payload must arrive unchanged once");
            }
        }
        if (!stopping)
            check(empty_received_[0] == accepted_empty && empty_received_[1] == accepted_empty,
                  "empty payload delivery count");
    }

private:
    std::vector<std::string> payloads_;
    std::vector<result> results_;
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::size_t completed_{};
    std::array<std::size_t, 2> received_{};
    std::array<std::size_t, 2> empty_received_{};
    bool invalid_payload_{};
    std::atomic<bool> queue_overflow_{};
    std::atomic<bool> submission_exception_{};
};

template <class Client, class Server> struct shutdown
{
    Client& client;
    Server& server;
    bool stopped{};
    void stop()
    {
        if (!stopped)
        {
            client.stop();
            server.stop();
            stopped = true;
        }
    }
    ~shutdown() { stop(); }
};

template <class Client, class Server>
void connect(Client& client, Server& server, outcomes& results, const std::string& certificates)
{
    client.set_auto_reconnect(false);
    client.set_disconnect_timeout(2s);
    if constexpr (requires { client.ssl_stream(); })
    {
        server.set_cert_file("", certificates + "/server.pem", certificates + "/server-key.pem", "");
        check(!arknet::get_last_error(), "concurrency server identity");
        client.load_verify_file(certificates + "/ca.pem");
    }
    server.bind_recv(
        [&](auto& session, std::string_view data)
        {
            results.receive(0, data);
            session->async_send(data);
        });
    client.bind_recv([&](std::string_view data) { results.receive(1, data); });
    if constexpr (requires { client.ws_stream(); })
    {
        check(server.start("127.0.0.1", 0), "concurrency WebSocket server start");
        check(client.start("127.0.0.1", server.get_listen_port(), "/concurrency"), "concurrency WebSocket connect");
    }
    else
    {
        check(server.start("127.0.0.1", 0, arknet::use_dgram), "concurrency TCP server start");
        check(client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram), "concurrency TCP connect");
    }
}

template <class Client>
void submit(Client& client, outcomes& results, std::size_t id, std::size_t limit, bool after_stop = false)
{
    try
    {
        const bool accepted = client.async_send(std::string_view(results.payload(id)),
                                                [&, id](const arknet::error_code& error, std::size_t bytes)
                                                { results.complete(id, error, bytes); });
        results.submitted(id, accepted, after_stop);
        results.queue_size(client.get_queued_send_buffer_size(), limit);
    }
    catch (...)
    {
        results.submission_threw();
    }
}

template <class Client, class Server>
void backpressure(Client& client, Server& server, bool operations, const std::string& certificates)
{
    const std::size_t rows = operations ? operation_limit * 2 / sender_count : 32;
    const std::size_t attempts = rows * sender_count;
    const std::size_t limit = 4 * payload_size;
    outcomes results(attempts + 1, operations);
    shutdown cleanup{client, server};
    connect(client, server, results, certificates);
    client.set_max_send_buffer_size(limit);
    io_pause pause(client);
    std::barrier start(static_cast<std::ptrdiff_t>(sender_count));
    std::vector<std::jthread> senders;
    for (std::size_t thread = 0; thread < sender_count; ++thread)
        senders.emplace_back(
            [&, thread]
            {
                start.arrive_and_wait();
                for (std::size_t row = 0; row < rows; ++row)
                    submit(client, results, thread * rows + row, limit);
            });
    for (auto& sender : senders)
        sender.join();
    const auto admitted = results.accepted();
    check(admitted == (operations ? operation_limit : limit / payload_size), "gated queue admission limit");
    pause.open();
    results.wait_completions(attempts);
    results.wait_delivery(admitted);
    check(client.get_queued_send_buffer_size() == 0, "queue drained after concurrent sends");
    submit(client, results, attempts, limit);
    results.wait_completions(attempts + 1);
    results.wait_delivery(admitted + 1);
    cleanup.stop();
    check(client.get_queued_send_buffer_size() == 0, "recovery send queue drained");
    results.verify(false);
}

template <class Client, class Server> void stop_race(Client& client, Server& server, bool from_callback)
{
    constexpr std::size_t racing_rows = 16;
    constexpr std::size_t late_rows = 16;
    constexpr std::size_t rows = 1 + racing_rows + late_rows;
    constexpr std::size_t limit = 2 * sender_count * payload_size;
    outcomes results(sender_count * rows);
    shutdown cleanup{client, server};
    connect(client, server, results, "");
    client.set_max_send_buffer_size(limit);
    io_pause pause(client);
    std::latch initial_sent(sender_count);
    std::latch race(1);
    std::latch racing_sent(sender_count);
    std::latch stopped(1);
    std::vector<std::jthread> senders;
    for (std::size_t thread = 0; thread < sender_count; ++thread)
        senders.emplace_back(
            [&, thread]
            {
                const auto base = thread * rows;
                submit(client, results, base, limit);
                initial_sent.count_down();
                race.wait();
                for (std::size_t row = 1; row <= racing_rows; ++row)
                    submit(client, results, base + row, limit);
                racing_sent.count_down();
                stopped.wait();
                for (std::size_t row = racing_rows + 1; row < rows; ++row)
                    submit(client, results, base + row, limit, true);
            });
    initial_sent.wait();
    std::promise<void> stop_done;
    auto completion = stop_done.get_future();
    latch_release release_senders{stopped};
    std::optional<std::jthread> owner;
    const auto request = [&]
    {
        try
        {
            client.stop();
            stop_done.set_value();
        }
        catch (...)
        {
            stop_done.set_exception(std::current_exception());
        }
    };
    if (from_callback)
        client.post(
            [&]
            {
                bool released{};
                try
                {
                    client.stop();
                    race.count_down();
                    released = true;
                    // Finish concurrent admission while stop completion is still pending.
                    racing_sent.wait();
                    stop_done.set_value();
                }
                catch (...)
                {
                    if (!released)
                        race.count_down();
                    stop_done.set_exception(std::current_exception());
                }
            });
    else
    {
        owner.emplace(
            [&]
            {
                race.wait();
                request();
            });
        race.count_down();
    }
    pause.open();
    await(completion);
    if (owner)
        owner->join();
    check(client.wait_stopped(), "concurrent send/stop owner completion");
    release_senders.open();
    for (auto& sender : senders)
        sender.join();
    results.wait_completions(sender_count * rows);
    cleanup.stop();
    check(client.get_queued_send_buffer_size() == 0, "cancelled concurrent queue drained");
    results.verify(true);
}

void stop_before_write()
{
    constexpr std::size_t size = 2 * 1024 * 1024;
    io_runner runner;
    asio::ip::tcp::acceptor listener(runner.context, {asio::ip::make_address("127.0.0.1"), 0});
    asio::ip::tcp::socket peer(runner.context);
    std::promise<arknet::error_code> accepted;
    auto connected = accepted.get_future();
    listener.async_accept(peer,
                          [&](arknet::error_code ec)
                          {
                              if (!ec)
                                  peer.set_option(asio::socket_base::receive_buffer_size(4096), ec);
                              accepted.set_value(ec);
                          });
    asio::steady_timer peer_watchdog(runner.context);
    std::atomic<bool> expired{};
    std::atomic<unsigned> calls{};
    std::promise<std::pair<arknet::error_code, std::size_t>> sent;
    auto completion = sent.get_future();
    arknet::tcp_client client(runner.context);
    client.set_auto_reconnect(false);
    client.set_disconnect_timeout(200ms);
    client.bind_init([&] { client.set_sndbuf_size(4096); });
    check(client.start("127.0.0.1", listener.local_endpoint().port()), "stop-before-write connect");
    check(!await(connected), "stop-before-write nonreading peer accept");
    peer_watchdog.expires_after(2s);
    peer_watchdog.async_wait(
        [&](const arknet::error_code& ec)
        {
            if (!ec)
            {
                expired = true;
                arknet::error_code ignored;
                peer.close(ignored);
            }
        });
    std::promise<std::pair<bool, bool>> posted;
    auto requested = posted.get_future();
    const auto started = std::chrono::steady_clock::now();
    client.post(
        [&, payload = std::string(size, 'x')]
        {
            const bool empty = client.get_queued_send_buffer_size() == 0;
            client.stop();
            // Inline send admission can precede the stop event queued by this callback.
            const bool admitted = client.async_send(payload,
                                                    [&](const arknet::error_code& ec, std::size_t bytes)
                                                    {
                                                        if (calls.fetch_add(1) == 0)
                                                            sent.set_value({ec, bytes});
                                                    });
            posted.set_value({empty, admitted});
        });
    const auto [empty, admitted] = await(requested);
    const auto [error, bytes] = await(completion);
    const bool stopped = client.wait_stopped();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    std::promise<void> cleaned;
    auto cleanup = cleaned.get_future();
    asio::post(runner.context,
               [&]
               {
                   arknet::error_code ignored;
                   peer_watchdog.cancel();
                   peer.close(ignored);
                   cleaned.set_value();
               });
    await(cleanup);
    check(empty && admitted, "stop-before-write must start with an empty queue and admit the inline send");
    check(!expired, "stop-before-write waits for the peer watchdog instead of its stop deadline");
    check(stopped && calls == 1 && bytes < size &&
              (error == asio::error::operation_aborted || error == asio::error::bad_descriptor ||
               error == asio::error::broken_pipe || error == asio::error::connection_reset ||
               error == asio::error::connection_aborted || error == asio::error::eof),
          "stop-before-write must cancel the blocked send exactly once");
    check(client.get_queued_send_buffer_size() == 0 && !runner.context.stopped(),
          "stop-before-write must drain its queue and preserve the external context");
    std::cout << "TCP external stop before write: " << elapsed.count() << " ms, peer watchdog=" << expired << '\n';
}

template <class Client, class Server, class Function> void transport(bool external, Function&& function)
{
    std::optional<io_runner> runner;
    if (external)
        runner.emplace();
    auto server = external ? std::make_unique<Server>(runner->context) : std::make_unique<Server>();
    auto client = external ? std::make_unique<Client>(runner->context) : std::make_unique<Client>();
    function(*client, *server);
    if (external)
        check(!runner->context.stopped(), "concurrent fixture must preserve external context");
}

template <class Client, class Server> void cases(const char* protocol)
{
    for (bool external : {false, true})
    {
        for (bool operations : {false, true})
            transport<Client, Server>(external, [&](auto& client, auto& server)
                                      { backpressure(client, server, operations, ""); });
        for (bool callback : {false, true})
            transport<Client, Server>(external,
                                      [&](auto& client, auto& server) { stop_race(client, server, callback); });
        std::cout << protocol << (external ? " external" : " owned")
                  << ": concurrent admission, recovery, and stop passed\n";
    }
}
}

DOCTEST_TEST_CASE("TCP external stop before a blocked write")
{
    watchdog timeout;
    stop_before_write();
}

namespace
{
DOCTEST_TEST_CASE("TCP auto reconnect rejects a send copied in the previous generation")
{
    watchdog timeout;
    class paused_client : public arknet::tcp_client_t<paused_client>
    {
    public:
        using arknet::tcp_client_t<paused_client>::tcp_client_t;
        std::promise<void> entered;
        std::latch released{1};

        std::uint64_t generation() { return this->life_id(); }

        std::string_view data_filter_before_send(std::string_view data)
        {
            if (data == "old-generation")
            {
                entered.set_value();
                released.wait();
            }
            return data;
        }
    };
    messages connections;
    messages received;
    std::promise<void> fresh_received;
    auto delivered = fresh_received.get_future();
    std::promise<std::pair<arknet::error_code, std::size_t>> old_sent;
    auto old_completion = old_sent.get_future();
    std::promise<bool> submitted;
    auto admission = submitted.get_future();
    std::atomic<unsigned> fresh_receipts{};
    std::atomic<unsigned> old_calls{};
    io_runner runner;
    arknet::tcp_server server;
    paused_client client(runner.context);
    shutdown cleanup{client, server};
    server.bind_connect([](auto& session) { session->set_disconnect_timeout(200ms); });
    server.bind_recv(
        [&](auto&, std::string_view data)
        {
            received.push(data);
            if (data == "current-generation" && fresh_receipts.fetch_add(1) == 0)
                fresh_received.set_value();
        });
    client.set_auto_reconnect(true, 20ms);
    client.set_disconnect_timeout(200ms);
    client.bind_connect(
        [&]
        {
            if (!arknet::get_last_error())
                connections.push(std::to_string(client.generation()));
        });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "generation server start");
    const auto port = server.get_listen_port();
    check(client.start("127.0.0.1", port, arknet::use_dgram), "generation client connect");
    const auto previous_generation = connections.wait(1).front();
    auto filtered = client.entered.get_future();
    std::jthread sender(
        [&]
        {
            try
            {
                submitted.set_value(client.async_send(std::string_view("old-generation"),
                                                      [&](const arknet::error_code& ec, std::size_t bytes)
                                                      {
                                                          if (old_calls.fetch_add(1) == 0)
                                                              old_sent.set_value({ec, bytes});
                                                      }));
            }
            catch (...)
            {
                submitted.set_exception(std::current_exception());
            }
        });
    latch_release release{client.released};
    await(filtered);
    // The paused submission owns IO pending work, so restart only the peer.
    server.stop();
    check(server.start("127.0.0.1", port, arknet::use_dgram), "generation server restart");
    const auto current_generation = connections.wait(2)[1];
    release.open();
    sender.join();
    check(await(admission), "generation old send was admitted before reconnect");
    const auto [old_error, old_bytes] = await(old_completion);
    auto fresh_sent = client.async_send(std::string("current-generation"), asio::use_future);
    const auto [fresh_error, fresh_bytes] = await(fresh_sent);
    await(delivered);
    cleanup.stop();
    const auto payloads = received.wait(1);
    std::cout << "TCP reconnect generation: " << previous_generation << " -> " << current_generation
              << ", old completion=" << old_error.message() << '/' << old_bytes << ", received=" << payloads.size()
              << '\n';
    DOCTEST_CHECK_MESSAGE(current_generation != previous_generation,
                          "successful reconnect publishes a new send generation");
    DOCTEST_CHECK_MESSAGE((old_calls == 1 && old_error == asio::error::operation_aborted && old_bytes == 0),
                          "previous-generation send completes once as operation_aborted without bytes");
    DOCTEST_CHECK_MESSAGE(
        (!fresh_error && fresh_bytes == std::string_view("current-generation").size() && fresh_receipts == 1),
        "current-generation send succeeds and reaches the peer once");
    DOCTEST_CHECK_MESSAGE(payloads == std::vector<std::string>{"current-generation"},
                          "previous-generation payload never reaches the reconnected peer");
    DOCTEST_CHECK_MESSAGE(client.get_queued_send_buffer_size() == 0, "generation test releases all send reservations");
}
}

DOCTEST_TEST_CASE("TCP dgram concurrent admission, recovery, and stop")
{
    watchdog timeout;
    cases<arknet::tcp_client, arknet::tcp_server>("TCP dgram");
}

DOCTEST_TEST_CASE("WebSocket concurrent admission, recovery, and stop")
{
    watchdog timeout;
    cases<arknet::ws_client, arknet::ws_server>("WebSocket");
}

#if defined(ARKNET_ENABLE_SSL) || defined(ARKNET_USE_SSL)
DOCTEST_TEST_CASE("WebSocket TLS concurrent admission and recovery")
{
    watchdog timeout;
    for (bool external : {false, true})
        transport<arknet::wss_client, arknet::wss_server>(
            external, [&](auto& client, auto& server) { backpressure(client, server, false, ARKNET_TEST_CERT_DIR); });
    std::cout << "WebSocket TLS owned/external: concurrent admission and recovery passed\n";
}
#endif

DOCTEST_TEST_CASE("One io_context with four workers serializes each IO lane")
{
    watchdog timeout;
    benchmark_runtime runtime("shared", 4);
    std::array<std::unique_ptr<arknet::timer>, 4> actors;
    std::array<std::atomic<unsigned>, 4> active{};
    std::array<unsigned, 4> counts{};
    std::atomic<unsigned> overlaps{}, wrong_executor{};
    std::barrier rendezvous(4);
    std::mutex workers_mutex;
    std::set<std::thread::id> workers;
    for (std::size_t i = 0; i < actors.size(); ++i)
    {
        actors[i] = std::make_unique<arknet::timer>(runtime.context_at());
        actors[i]->post(
            [&, i]
            {
                {
                    std::lock_guard lock(workers_mutex);
                    workers.insert(std::this_thread::get_id());
                }
                rendezvous.arrive_and_wait();
            });
    }
    for (std::size_t i = 0; i < actors.size(); ++i)
        for (unsigned task = 0; task < 200; ++task)
            actors[i]->post(
                [&, i]
                {
                    if (active[i].fetch_add(1) != 0)
                        ++overlaps;
                    if (!actors[i]->running_in_this_thread())
                        ++wrong_executor;
                    ++counts[i];
                    std::this_thread::sleep_for(10us);
                    --active[i];
                });
    for (auto& actor : actors)
    {
        auto drained = actor->post([] {}, asio::use_future);
        await(drained);
        actor->stop();
    }
    check(workers.size() == 4, "all four workers run the same context");
    check(overlaps == 0 && wrong_executor == 0, "each lane preserves executor affinity without overlap");
    check(std::all_of(counts.begin(), counts.end(), [](unsigned count) { return count == 200; }),
          "all lane-local tasks complete");
}
