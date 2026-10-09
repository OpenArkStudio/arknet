#include <arknet/udp/udp_client.hpp>
#include <arknet/udp/udp_server.hpp>
#include <arknet/udp/udp_cast.hpp>
#include "check.hpp"

#include <array>
#include <atomic>
#include <barrier>
#include <latch>
#include <memory>
#include <thread>

namespace
{
struct udp_send_probe : arknet::detail::udp_send_op<udp_send_probe, void>
{
    struct test_io
    {
        bool running_in_this_thread() const { return true; }
    } context;
    test_io* io_ = &context;
    struct test_stream
    {
        arknet::error_code error;
        std::size_t count{};
        template <class Buffer, class Handler> void async_send(Buffer, Handler&& handler) { handler(error, count); }
        template <class Buffer, class Handler>
        void async_send_to(Buffer buffer, const asio::ip::udp::endpoint&, Handler&& handler)
        {
            async_send(buffer, std::forward<Handler>(handler));
        }
    } socket;
    arknet::detail::handler_memory<> memory;
    auto& stream() { return socket; }
    auto& wallocator() { return memory; }
    using udp_send_op::_udp_send;
    using udp_send_op::_udp_send_to;
};
}

DOCTEST_TEST_CASE("UDP send completion reports zero bytes on failure")
{
    const std::array<std::pair<arknet::error_code, std::size_t>, 5> results{
        {{asio::error::message_size, 65508},
         {arknet::error_code(1784, asio::error::get_system_category()), 65508},
         {asio::error::operation_aborted, 8},
         {{}, 8},
         {{}, 0}}};
    for (const auto& [error, count] : results)
    {
        for (const bool connected : {false, true})
        {
            DOCTEST_INFO("connected=", connected, ", error=", error.value(), ", bytes=", count);
            udp_send_probe sender;
            sender.socket.error = error;
            sender.socket.count = count;
            int calls = 0;
            auto completion = [&, capture = std::make_unique<int>(42)](const arknet::error_code& actual_error,
                                                                       std::size_t actual_count)
            {
                ++calls;
                check(*capture == 42, "UDP completion supports move-only captures");
                check(actual_error == error, "UDP completion preserves the original error");
                check(actual_count == (error ? 0 : count), "UDP failure has no transmitted bytes");
            };
            std::string payload(8, 'u');
            const bool accepted = connected
                                      ? sender._udp_send(payload, std::move(completion))
                                      : sender._udp_send_to(asio::ip::udp::endpoint{}, payload, std::move(completion));
            check(accepted && calls == 1, "UDP send completes exactly once");
        }
    }
}

struct udp_io_runner
{
    asio::io_context context;
    asio::executor_work_guard<asio::io_context::executor_type> work{context.get_executor()};
    std::thread thread{[this] { context.run(); }};
    void finish()
    {
        work.reset();
        context.stop();
        if (thread.joinable())
            thread.join();
    }
    ~udp_io_runner() { finish(); }
};

DOCTEST_TEST_CASE("UDP sessions preserve first, empty and binary datagrams across restart")
{
    messages received;
    messages second_received;
    std::atomic<int> connects{};
    std::atomic<unsigned short> second_endpoint{};
    arknet::udp_server server(1024, 65536);
    server.bind_connect([&](auto&) { ++connects; });
    server.bind_recv(
        [&](auto& session, std::string_view data)
        {
            if (data.size() == 8192)
                second_endpoint = session->get_remote_port();
            session->async_send(data);
        });
    arknet::udp_client client(1024, 65536);
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view data) { received.push(data); });
    arknet::udp_client second(1024, 65536);
    second.set_auto_reconnect(false);
    second.bind_recv([&](std::string_view data) { second_received.push(data); });
    const std::vector<std::string> expected{"", std::string("a\0b", 3), "third"};
    for (std::size_t round = 0; round < 2; ++round)
    {
        check(server.start("127.0.0.1", 0), "UDP server start/restart");
        check(client.start("127.0.0.1", server.get_listen_port()), "UDP connect/reconnect");
        check(second.start("127.0.0.1", server.get_listen_port()), "second UDP connect");
        const auto first_port = client.get_local_port();
        const auto second_port = second.get_local_port();
        check(first_port != second_port, "UDP clients need distinct endpoints");
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            client.async_send(expected[i]);
            const auto count = round * expected.size() + i + 1;
            check(received.wait(count)[count - 1] == expected[i], "UDP first/empty/binary datagrams survive");
        }
        second.async_send(std::string(8192, 'u'));
        check(second_received.wait(round + 1)[round] == std::string(8192, 'u'),
              "first UDP datagram larger than the initial buffer survives");
        check(second_endpoint == second_port, "UDP session uses the correct remote endpoint");
        check(connects == static_cast<int>((round + 1) * 2) && server.get_session_count() == 2,
              "UDP datagrams reuse endpoint sessions");
        client.stop();
        second.stop();
        server.stop();
        check(server.get_session_count() == 0, "UDP stop clears sessions");
    }
}

DOCTEST_TEST_CASE("UDP cast owns payloads and completes rejected and resolved sends")
{
    messages cast_received;
    std::atomic<unsigned short> sender_endpoint{};
    std::atomic<unsigned short> responder_endpoint{};
    arknet::udp_cast receiver(1024, 65536);
    receiver.bind_recv(
        [&](auto& endpoint, std::string_view data)
        {
            sender_endpoint = endpoint.port();
            receiver.async_send(endpoint, data);
        });
    check(receiver.start("127.0.0.1", 0), "UDP cast receiver start");
    arknet::udp_cast sender;
    sender.bind_recv(
        [&](auto& endpoint, std::string_view data)
        {
            responder_endpoint = endpoint.port();
            cast_received.push(data);
        });
    check(sender.start("127.0.0.1", 0), "UDP cast sender start");
    std::promise<std::pair<arknet::error_code, std::size_t>> sent;
    auto sent_result = sent.get_future();
    check(sender.async_send("localhost", receiver.get_local_port(), std::string(8192, 'c'),
                            [&](const arknet::error_code& ec, std::size_t count) { sent.set_value({ec, count}); }),
          "UDP cast accepts hostname send");
    const auto send_result = await(sent_result);
    check(!send_result.first && send_result.second == 8192, "UDP cast error/bytes completion");
    check(cast_received.wait(1).front() == std::string(8192, 'c'), "UDP cast routes full first payload");
    check(sender_endpoint == sender.get_local_port() && responder_endpoint == receiver.get_local_port(),
          "UDP cast preserves both endpoints");

    const auto target = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), receiver.get_local_port());
    sender.set_max_send_buffer_size(4);
    std::promise<void> queue_blocked;
    auto blocked = queue_blocked.get_future();
    std::promise<void> release_queue;
    auto release = release_queue.get_future();
    sender.post(
        [&]
        {
            queue_blocked.set_value();
            release.wait();
        });
    await(blocked);
    auto held = sender.async_send(target, std::string("hold"), asio::use_future);
    std::atomic<int> rejection_calls{};
    const bool accepted = sender.async_send(target, std::string("x"),
                                            [&](const arknet::error_code& ec, std::size_t count)
                                            {
                                                if (ec == asio::error::no_buffer_space && count == 0)
                                                    ++rejection_calls;
                                            });
    const auto queued_bytes = sender.get_queued_send_buffer_size();
    release_queue.set_value();
    check(!accepted && rejection_calls == 1 && queued_bytes == 4, "cast queue refuses excess bytes exactly once");
    check(!await(held).first, "cast queued send completes after release");
    check(cast_received.wait(2)[1] == "hold", "cast recovers after queue rejection");
    check(sender.get_queued_send_buffer_size() == 0, "cast completion releases reserved bytes");

    sender.set_max_send_buffer_size(16);
    std::string source = "copied";
    auto copied = sender.async_send(target, asio::const_buffer(source.data(), source.size()), asio::use_future);
    source.assign(source.size(), 'x');
    check(!await(copied).first, "cast const_buffer send completes");
    check(cast_received.wait(3)[2] == "copied", "cast owns borrowed input until completion");
    auto invalid = sender.async_send(target, static_cast<const char*>(nullptr), 1, asio::use_future);
    check(await(invalid).first == asio::error::invalid_argument, "cast rejects null input");
    auto bad_service = sender.async_send("127.0.0.1", "arknet-invalid-service", "x", asio::use_future);
    check(static_cast<bool>(await(bad_service).first), "cast resolver failure completes");
    sender.stop();
    receiver.stop();

    auto offline = sender.async_send(target, std::string("x"), asio::use_future);
    check(await(offline).first == asio::error::not_connected, "stopped cast send completes");
}

DOCTEST_TEST_CASE("UDP endpoints reject receive limits that truncate datagrams")
{
    arknet::udp_server small_server(1024, 8192);
    check(!small_server.start("127.0.0.1", 0) && arknet::get_last_error() == asio::error::message_size,
          "UDP server rejects truncating receive limits");
    arknet::udp_client small_client(1024, 8192);
    check(!small_client.start("127.0.0.1", 1) && arknet::get_last_error() == asio::error::message_size,
          "UDP client rejects truncating receive limits");
    arknet::udp_cast small_cast(1024, 8192);
    check(!small_cast.start("127.0.0.1", 0) && arknet::get_last_error() == asio::error::message_size,
          "UDP cast rejects truncating receive limits");
}

DOCTEST_TEST_CASE("UDP callbacks stop and restart without stopping an external IO context")
{
    const auto target = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 1);
    udp_io_runner runner;
    arknet::udp_server shared_server(runner.context);
    shared_server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(shared_server.start("127.0.0.1", 0), "shared UDP server start");
    arknet::udp_client shared_client(runner.context);
    shared_client.set_auto_reconnect(false);
    std::promise<void> callback_stopped;
    auto callback_result = callback_stopped.get_future();
    shared_client.bind_recv(
        [&](std::string_view)
        {
            shared_client.stop();
            callback_stopped.set_value();
        });
    check(shared_client.start("127.0.0.1", shared_server.get_listen_port()), "shared UDP connect");
    shared_client.async_send("stop");
    await(callback_result);
    check(shared_client.wait_stopped(), "UDP owner waits for callback stop");
    shared_server.request_stop();
    check(shared_server.wait_stopped(), "UDP external server stop waits for sessions");
    arknet::udp_cast shared_cast(runner.context);
    check(shared_cast.start("127.0.0.1", 0), "shared cast start");
    std::promise<void> cast_stopped;
    auto cast_result = cast_stopped.get_future();
    shared_cast.post(
        [&]
        {
            shared_cast.stop();
            cast_stopped.set_value();
        });
    await(cast_result);
    check(shared_cast.wait_stopped(), "cast owner waits for callback stop");
    check(shared_cast.start("127.0.0.1", 0), "shared cast restarts after callback stop");
    shared_cast.set_max_send_buffer_size(1);
    std::atomic<int> caller_stop_callbacks{};
    const bool caller_accepted = shared_cast.async_send(target, std::string("overflow"),
                                                        [&](const arknet::error_code& ec, std::size_t count)
                                                        {
                                                            shared_cast.stop();
                                                            if (ec == asio::error::no_buffer_space && count == 0)
                                                                ++caller_stop_callbacks;
                                                        });
    check(!caller_accepted && caller_stop_callbacks == 1 && shared_cast.wait_stopped(),
          "cast queue rejection callback can stop on the caller thread");
    check(!runner.context.stopped(), "UDP stop preserves host IO context");
    std::promise<void> host_alive;
    auto alive = host_alive.get_future();
    asio::post(runner.context, [&] { host_alive.set_value(); });
    await(alive);
}

DOCTEST_TEST_CASE("UDP cast concurrent admission and stop complete every submission exactly once")
{
    constexpr std::size_t threads = 8;
    constexpr std::size_t payload_size = 256;
    constexpr std::size_t admission_rows = 16;
    constexpr std::size_t admission_attempts = threads * admission_rows;
    constexpr std::size_t race_rows = 34;
    constexpr std::size_t race_offset = admission_attempts + 1;
    constexpr std::size_t attempts = race_offset + threads * race_rows;
    struct result
    {
        bool submitted{};
        bool accepted{};
        bool after_stop{};
        std::size_t callbacks{};
        std::size_t bytes{};
        arknet::error_code error;
    };
    std::array<result, attempts> results{};
    std::mutex result_mutex;
    std::condition_variable completed;
    std::size_t completions{};
    std::atomic<unsigned> submission_exceptions{};
    std::atomic<bool> queue_overflow{};
    std::atomic<bool> stop_exception{};
    udp_io_runner runner;
    arknet::udp_cast receiver(runner.context);
    receiver.bind_recv([](auto&, std::string_view) {});
    check(receiver.start("127.0.0.1", 0), "concurrent cast receiver start");
    const auto target = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), receiver.get_local_port());
    arknet::udp_cast sender(runner.context);
    check(sender.start("127.0.0.1", 0), "concurrent cast sender start");
    std::size_t limit = 4 * payload_size;
    sender.set_max_send_buffer_size(limit);
    const auto submit = [&](std::size_t id, bool after_stop = false)
    {
        try
        {
            std::string payload(payload_size, static_cast<char>('A' + id % 26));
            const bool accepted = sender.async_send(target, asio::const_buffer(payload.data(), payload.size()),
                                                    [&, id](const arknet::error_code& error, std::size_t bytes)
                                                    {
                                                        {
                                                            std::lock_guard lock(result_mutex);
                                                            auto& value = results[id];
                                                            ++value.callbacks;
                                                            value.error = error;
                                                            value.bytes = bytes;
                                                            ++completions;
                                                        }
                                                        completed.notify_all();
                                                    });
            payload.assign(payload.size(), 'x');
            {
                std::lock_guard lock(result_mutex);
                results[id].submitted = true;
                results[id].accepted = accepted;
                results[id].after_stop = after_stop;
            }
            if (sender.get_queued_send_buffer_size() > limit)
                queue_overflow = true;
        }
        catch (...)
        {
            ++submission_exceptions;
        }
    };
    const auto wait_completions = [&](std::size_t count)
    {
        std::unique_lock lock(result_mutex);
        return completed.wait_for(lock, 5s, [&] { return completions >= count; });
    };

    auto paused = std::make_shared<std::promise<void>>();
    auto entered = paused->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto released = release->get_future().share();
    sender.post(
        [paused, released]
        {
            paused->set_value();
            released.wait();
        });
    const bool io_paused = entered.wait_for(5s) == std::future_status::ready;
    if (!io_paused)
        release->set_value();
    check(io_paused, "cast IO admission gate entered");
    std::barrier start(static_cast<std::ptrdiff_t>(threads));
    std::vector<std::jthread> senders;
    for (std::size_t thread = 0; thread < threads; ++thread)
        senders.emplace_back(
            [&, thread]
            {
                start.arrive_and_wait();
                for (std::size_t row = 0; row < admission_rows; ++row)
                    submit(thread * admission_rows + row);
            });
    for (auto& thread : senders)
        thread.join();
    std::size_t admitted{};
    {
        std::lock_guard lock(result_mutex);
        for (std::size_t id = 0; id < admission_attempts; ++id)
            admitted += results[id].accepted;
    }
    const auto queued = sender.get_queued_send_buffer_size();
    release->set_value();
    check(admitted == 4 && queued == limit, "concurrent cast admission enforces the exact byte limit");
    check(wait_completions(admission_attempts), "concurrent cast admission completion timeout");
    auto drained = sender.post([] {}, asio::use_future);
    await(drained);
    check(sender.get_queued_send_buffer_size() == 0, "concurrent cast completion releases all reserved bytes");
    submit(admission_attempts);
    check(wait_completions(race_offset), "cast recovery completion timeout");
    {
        std::lock_guard lock(result_mutex);
        const auto& recovery = results[admission_attempts];
        check(recovery.accepted && !recovery.error && recovery.bytes == payload_size,
              "cast accepts and completes a new send after backpressure drains");
    }
    check(sender.get_queued_send_buffer_size() == 0, "cast recovery send leaves an empty queue");

    limit = 2 * threads * payload_size;
    sender.set_max_send_buffer_size(limit);
    paused = std::make_shared<std::promise<void>>();
    entered = paused->get_future();
    release = std::make_shared<std::promise<void>>();
    released = release->get_future().share();
    sender.post(
        [paused, released]
        {
            paused->set_value();
            released.wait();
        });
    const bool race_paused = entered.wait_for(5s) == std::future_status::ready;
    if (!race_paused)
        release->set_value();
    check(race_paused, "cast IO stop-race gate entered");
    std::latch seeded(threads);
    std::latch stopped(1);
    std::barrier race(static_cast<std::ptrdiff_t>(threads + 1));
    senders.clear();
    for (std::size_t thread = 0; thread < threads; ++thread)
        senders.emplace_back(
            [&, thread]
            {
                const auto base = race_offset + thread * race_rows;
                submit(base);
                seeded.count_down();
                race.arrive_and_wait();
                for (std::size_t row = 1; row + 1 < race_rows; ++row)
                    submit(base + row);
                stopped.wait();
                submit(base + race_rows - 1, true);
            });
    seeded.wait();
    const auto seeded_bytes = sender.get_queued_send_buffer_size();
    std::jthread stopper(
        [&]
        {
            race.arrive_and_wait();
            try
            {
                sender.stop();
            }
            catch (...)
            {
                stop_exception = true;
            }
            stopped.count_down();
        });
    release->set_value();
    for (auto& thread : senders)
        thread.join();
    stopper.join();
    check(seeded_bytes == threads * payload_size, "cast stop race starts with admitted queued sends");
    check(!stop_exception && sender.wait_stopped(), "concurrent cast stop completes");
    check(wait_completions(attempts), "cast stop-race completion timeout");
    check(sender.get_queued_send_buffer_size() == 0, "cast stop releases every queued reservation");
    check(!runner.context.stopped(), "cast stop preserves the external IO context");
    std::promise<void> host_alive;
    auto alive = host_alive.get_future();
    asio::post(runner.context, [&] { host_alive.set_value(); });
    await(alive);
    receiver.stop();

    check(submission_exceptions == 0 && !queue_overflow,
          "cast concurrent submissions do not throw or exceed the byte limit");
    std::lock_guard lock(result_mutex);
    for (std::size_t id = 0; id < attempts; ++id)
    {
        DOCTEST_CAPTURE(id);
        const auto& value = results[id];
        DOCTEST_INFO("accepted=", value.accepted, ", callbacks=", value.callbacks, ", error=", value.error.value(), " ",
                     value.error.message(), ", bytes=", value.bytes);
        check(value.submitted && value.callbacks == 1, "every cast submission completes exactly once");
        if (!value.accepted)
            check(value.bytes == 0 && (value.error == asio::error::no_buffer_space ||
                                       (id >= race_offset && value.error == asio::error::not_connected)),
                  "cast rejection reports a queue or stopped-state error without bytes");
        else if (!value.error)
            check(value.bytes == payload_size, "successful cast send reports the full payload size");
        else
            check(id >= race_offset && value.bytes <= payload_size &&
                      (value.error == asio::error::operation_aborted || value.error == asio::error::not_connected ||
                       value.error == asio::error::bad_descriptor || value.error == asio::error::connection_aborted ||
                       value.error == asio::error::connection_reset || value.error == asio::error::broken_pipe),
                  "accepted cast send reports only a valid stop cancellation");
        if (value.after_stop)
            check(!value.accepted && value.error == asio::error::not_connected,
                  "cast sends after completed stop reject as not_connected");
    }
}

DOCTEST_TEST_CASE("UDP cast preserves empty, binary, and maximum IPv4 payloads")
{
    messages received;
    arknet::udp_cast receiver;
    receiver.bind_recv([&](auto&, std::string_view bytes) { received.push(bytes); });
    check(receiver.start("127.0.0.1", 0), "datagram receiver start");
    arknet::udp_cast sender;
    sender.bind_init([&] { sender.socket().set_option(asio::socket_base::send_buffer_size(131072)); });
    check(sender.start("127.0.0.1", 0), "datagram sender start");
    auto endpoint = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), receiver.get_local_port());
    const std::vector<std::string> payloads{"", std::string("a\0z", 3), std::string(65507, 'm')};
    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        auto completion = sender.async_send(endpoint, payloads[index], asio::use_future);
        const auto [error, count] = await(completion);
        check(!error && count == payloads[index].size(), "UDP send reports every payload byte");
        check(received.wait(index + 1)[index] == payloads[index], "unconnected receive preserves complete datagrams");
    }
    const char unterminated[]{'b', 'o', 'u', 'n', 'd'};
    auto array_sent = sender.async_send(endpoint, unterminated, asio::use_future);
    check(!await(array_sent).first && received.wait(4)[3] == "bound",
          "character array inspection stays within its extent");
    auto too_large = sender.async_send(endpoint, std::string(65508, 'x'), asio::use_future);
    const auto [too_large_error, too_large_count] = await(too_large);
    DOCTEST_INFO("error=", too_large_error.value(), ", category=", too_large_error.category().name(),
                 ", message=", too_large_error.message(), ", bytes=", too_large_count);
    check(too_large_error && too_large_count == 0, "oversized UDP payload fails without reporting transmitted bytes");
    auto recovery = sender.async_send(endpoint, std::string("recovered"), asio::use_future);
    check(!await(recovery).first && received.wait(5)[4] == "recovered",
          "oversized send does not block the next datagram");
    check(sender.get_queued_send_buffer_size() == 0, "all datagram send reservations are released");
    sender.stop();
    receiver.stop();
}

DOCTEST_TEST_CASE("UDP cast rejects a send copied before callback stop and restart")
{
    class filtered_cast : public arknet::udp_cast_t<filtered_cast>
    {
    public:
        using arknet::udp_cast_t<filtered_cast>::udp_cast_t;
        std::promise<void> entered;
        std::latch release{1};
        std::string_view data_filter_before_send(std::string_view bytes)
        {
            if (bytes == "previous")
            {
                entered.set_value();
                release.wait();
            }
            return bytes;
        }
    };
    messages received;
    messages started;
    udp_io_runner runner;
    arknet::udp_cast receiver(runner.context);
    receiver.bind_recv([&](auto&, std::string_view bytes) { received.push(bytes); });
    check(receiver.start("127.0.0.1", 0), "generation receiver start");
    filtered_cast sender(runner.context);
    sender.bind_start(
        [&]
        {
            if (!arknet::get_last_error())
                started.push("started");
        });
    check(sender.start("127.0.0.1", 0), "generation sender start");
    const auto target = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), receiver.get_local_port());
    std::promise<std::pair<arknet::error_code, std::size_t>> sent;
    auto completion = sent.get_future();
    auto paused = sender.entered.get_future();
    std::jthread submitter(
        [&]
        {
            sender.async_send(target, std::string_view("previous"),
                              [&](const arknet::error_code& error, std::size_t count)
                              { sent.set_value({error, count}); });
        });
    struct unlock
    {
        std::latch& latch;
        bool armed = true;
        void open()
        {
            if (std::exchange(armed, false))
                latch.count_down();
        }
        ~unlock() { open(); }
    } release{sender.release};
    await(paused);
    sender.post(
        [&]
        {
            sender.stop();
            sender.async_start("127.0.0.1", 0);
        });
    started.wait(2);
    release.open();
    submitter.join();
    const auto [error, count] = await(completion);
    check(error == asio::error::operation_aborted && count == 0, "old-generation datagram is cancelled");
    auto fresh = sender.async_send(target, std::string("current"), asio::use_future);
    check(!await(fresh).first, "new generation accepts datagrams");
    check(received.wait(1) == std::vector<std::string>{"current"}, "only new-generation data reaches the peer");
    sender.stop();
    receiver.stop();
    check(sender.get_queued_send_buffer_size() == 0 && !runner.context.stopped(),
          "restart releases reservations and preserves host IO");
}

DOCTEST_TEST_CASE("UDP custom session can stop during connect without delivering its first datagram")
{
    class custom_session : public arknet::udp_session_t<custom_session>
    {
    public:
        using arknet::udp_session_t<custom_session>::udp_session_t;
    };
    std::atomic<std::size_t> connect_count{};
    std::atomic<std::size_t> receive_count{};
    std::promise<void> rejected;
    auto disconnected = rejected.get_future();
    arknet::udp_server_t<custom_session> server;
    server.bind_connect(
        [&](auto& session)
        {
            if (++connect_count == 1)
            {
                session->stop();
                rejected.set_value();
            }
        });
    server.bind_recv(
        [&](auto& session, std::string_view bytes)
        {
            ++receive_count;
            session->async_send(bytes);
        });
    check(server.start("127.0.0.1", 0), "custom session server start");
    arknet::udp_client client;
    client.set_auto_reconnect(false);
    messages received;
    client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
    check(client.start("127.0.0.1", server.get_listen_port()), "custom session client connect");
    client.async_send("rejected");
    await(disconnected);
    client.async_send("accepted");
    check(received.wait(1).front() == "accepted", "next datagram creates a usable session");
    check(connect_count == 2 && receive_count == 1, "stopped first session never delivers its first payload");
    client.stop();
    server.stop();
}

class udp_delayed_session : public arknet::udp_session_t<udp_delayed_session>
{
public:
    using arknet::udp_session_t<udp_delayed_session>::udp_session_t;
    template <class Data, class Completion> bool _do_send(Data& data, Completion&& completion)
    {
        auto timer = std::make_shared<asio::steady_timer>(this->io_->executor());
        timer->expires_after(std::chrono::milliseconds(50));
        timer->async_wait(
            [this, timer, &data, completion = std::forward<Completion>(completion)](const arknet::error_code&) mutable
            { this->_udp_send_to(this->remote_endpoint_, data, std::move(completion)); });
        return true;
    }
};

DOCTEST_TEST_CASE("UDP session routing does not wait for a connect callback send")
{
    std::atomic<std::size_t> connects{};
    messages requests;
    arknet::udp_server_t<udp_delayed_session> server;
    server.bind_connect(
        [&](auto& session)
        {
            ++connects;
            session->async_send("welcome");
        });
    server.bind_recv([&](auto&, std::string_view bytes) { requests.push(bytes); });
    check(server.start("127.0.0.1", 0), "delayed write UDP server start");
    arknet::udp_client client;
    client.set_auto_reconnect(false);
    check(client.start("127.0.0.1", server.get_listen_port()), "delayed write UDP client start");
    client.async_send("one");
    client.async_send("two");
    check((requests.wait(2) == std::vector<std::string>{"one", "two"}),
          "routing retains first and subsequent datagrams");
    check(connects == 1 && server.get_session_count() == 1,
          "one endpoint creates one session while the welcome write is pending");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("UDP cast request_stop does not retain an already stopped shared object")
{
    for (const bool was_started : {false, true})
    {
        std::weak_ptr<arknet::udp_cast> lifetime;
        {
            auto endpoint = std::make_shared<arknet::udp_cast>();
            if (was_started)
            {
                check(endpoint->start("127.0.0.1", 0), "shared UDP cast start");
                endpoint->stop();
            }
            check(endpoint->is_stopped(), "cast is fully stopped before request_stop");
            lifetime = endpoint;
            endpoint->request_stop();
        }
        check(lifetime.expired(), "stopped cast has no pending callback ownership cycle");
    }
}

DOCTEST_TEST_CASE("UDP repeated stop after callback stop does not require a running external context")
{
    udp_io_runner runner;
    auto server = std::make_shared<arknet::udp_server>(runner.context);
    auto client = std::make_shared<arknet::udp_client>(runner.context);
    auto cast = std::make_shared<arknet::udp_cast>(runner.context);
    std::weak_ptr<arknet::udp_server> server_lifetime = server;
    std::weak_ptr<arknet::udp_client> client_lifetime = client;
    std::weak_ptr<arknet::udp_cast> cast_lifetime = cast;
    std::promise<void> server_done;
    auto server_stopped = server_done.get_future();
    std::promise<void> cast_done;
    auto cast_stopped = cast_done.get_future();
    std::promise<void> client_done;
    auto client_stopped = client_done.get_future();
    server->bind_stop([&] { server_done.set_value(); });
    cast->bind_stop([&] { cast_done.set_value(); });
    client->set_auto_reconnect(false);
    check(server->start("127.0.0.1", 0), "repeat-stop UDP server start");
    check(client->start("127.0.0.1", server->get_listen_port()), "repeat-stop UDP client start");
    check(cast->start("127.0.0.1", 0), "repeat-stop UDP cast start");
    server->post(
        [&]
        {
            client->stop();
            server->stop();
            cast->stop();
            client->post_queued_event([&] { client_done.set_value(); });
        });
    await(client_stopped);
    await(server_stopped);
    await(cast_stopped);
    runner.finish();
    check(client->is_stopped() && server->is_stopped() && cast->is_stopped(),
          "UDP callback stops finish before host shutdown");
    client->stop();
    server->stop();
    cast->stop();
    client.reset();
    server.reset();
    cast.reset();
    check(client_lifetime.expired() && server_lifetime.expired() && cast_lifetime.expired(),
          "repeated stop and destruction do not post ownership cycles into stopped IO");
}

DOCTEST_TEST_CASE("UDP cast accepts generic move-only send completions")
{
    arknet::udp_cast receiver;
    messages received;
    receiver.bind_recv([&](auto&, std::string_view bytes) { received.push(bytes); });
    check(receiver.start("127.0.0.1", 0), "generic callback receiver start");
    arknet::udp_cast sender;
    check(sender.start("127.0.0.1", 0), "generic callback sender start");
    const auto target = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), receiver.get_local_port());
    std::promise<std::pair<arknet::error_code, std::size_t>> direct_done;
    auto direct = direct_done.get_future();
    check(sender.async_send(target, "direct",
                            [state = std::make_unique<int>(6), &direct_done](const auto& error, auto count) mutable
                            {
                                if (static_cast<std::size_t>(*state) == count)
                                    direct_done.set_value({error, count});
                                else
                                    direct_done.set_value({asio::error::fault, count});
                            }),
          "endpoint send accepts a generic move-only callback");
    check(!await(direct).first, "generic two-argument send completes");
    std::promise<std::size_t> resolved_done;
    auto resolved = resolved_done.get_future();
    check(sender.async_send("localhost", receiver.get_local_port(), "resolved",
                            [&resolved_done](auto count) { resolved_done.set_value(count); }),
          "hostname send accepts a generic byte-count callback");
    check(await(resolved) == 8, "generic single-argument send completes with bytes");
    std::promise<void> empty_done;
    auto empty = empty_done.get_future();
    check(sender.async_send(target, std::string{}, [&empty_done] { empty_done.set_value(); }),
          "send accepts a zero-argument callback");
    await(empty);
    check((received.wait(3) == std::vector<std::string>{"direct", "resolved", ""}),
          "generic callback submissions deliver owned datagrams");
    sender.stop();
    receiver.stop();
}
