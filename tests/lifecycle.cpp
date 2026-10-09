#include <arknet/arknet.hpp>
#include "check.hpp"

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

struct late_deadline_session : arknet::tcp_session_t<late_deadline_session>
{
    using super = arknet::tcp_session_t<late_deadline_session>;
    using super::super;

    template <class Chain>
    void _post_close(const arknet::error_code& error, std::shared_ptr<late_deadline_session> lifetime,
                     arknet::detail::state_t previous, Chain chain)
    {
        // Force a queued stop deadline to arrive after transport shutdown.
        this->_arm_stop_deadline();
        super::_post_close(error, std::move(lifetime), previous, std::move(chain));
    }
};

DOCTEST_TEST_CASE("A late stop deadline cannot retain a closing TCP session's IO worker")
{
    messages transitions;
    arknet::tcp_server_t<late_deadline_session> server(1024, 16 * 1024 * 1024, 2);
    server.bind_accept([](auto& session) { session->set_disconnect_timeout(2s); });
    server.bind_connect([&](auto&) { transitions.push("connected"); });
    server.bind_disconnect([&](auto&) { transitions.push("disconnected"); });
    check(server.start("127.0.0.1", 0), "late deadline server starts");
    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    check(client.start("127.0.0.1", server.get_listen_port()), "late deadline client connects");
    check(transitions.wait(1).front() == "connected", "late deadline session is established");
    client.stop();
    check(transitions.wait(2).back() == "disconnected", "peer shutdown reaches the close path");
    const auto began = std::chrono::steady_clock::now();
    server.stop();
    check(std::chrono::steady_clock::now() - began < 1s,
          "closing session releases its worker without waiting for the stop deadline");
}

DOCTEST_TEST_CASE("Endpoint and string conversion report valid values and invalid services")
{
    check(arknet::to_string('x') == "x", "char conversion");
    check(arknet::to_string(static_cast<const char*>(nullptr)).empty(), "null pointer conversion");
    check(arknet::to_string(12345) == "12345", "port conversion");
    check(arknet::to_string(std::string("text")) == "text", "string conversion");
    check(arknet::to_string(std::string_view("view")) == "view", "string_view conversion");
    check(arknet::to_string_view(static_cast<const char*>(nullptr)).empty(), "null string_view conversion");
    const auto tcp_endpoint = arknet::to_endpoint<asio::ip::tcp::endpoint>("127.0.0.1", 12345);
    check(!arknet::get_last_error() && tcp_endpoint.port() == 12345 && tcp_endpoint.address().is_loopback(),
          "TCP endpoint resolution");
    arknet::to_endpoint<asio::ip::tcp::endpoint>("127.0.0.1", "arknet-invalid-service");
    check(static_cast<bool>(arknet::get_last_error()), "invalid TCP service must fail");
    const auto udp_endpoint = arknet::to_endpoint<asio::ip::udp::endpoint>("127.0.0.1", 12345);
    check(!arknet::get_last_error() && udp_endpoint.port() == 12345 && udp_endpoint.address().is_loopback(),
          "UDP endpoint resolution");
    arknet::to_endpoint<asio::ip::udp::endpoint>("127.0.0.1", "arknet-invalid-service");
    check(static_cast<bool>(arknet::get_last_error()), "invalid UDP service must fail");
}

DOCTEST_TEST_CASE("Offline TCP sends complete and event getters reject cross-thread access")
{
    arknet::tcp_client offline;
    check(offline.get_pending_event_count() == 0 && arknet::get_last_error() == asio::error::operation_not_supported,
          "pending event getter rejects cross-thread access");
    auto completion = offline.async_send(std::string("offline"), asio::use_future);
    const auto result = await(completion);
    check(result.first == asio::error::not_connected && result.second == 0, "stopped client send must complete");
}

DOCTEST_TEST_CASE("Timer and post callbacks run in order")
{
    messages callbacks;
    arknet::timer timer;
    timer.post([&] { callbacks.push("post"); });
    timer.start_timer("once", 10ms, 1, [&] { callbacks.push("timer"); });
    const auto called = callbacks.wait(2);
    check(called[0] == "post" && called[1] == "timer", "timer and post callbacks");
    timer.stop();
}

DOCTEST_TEST_CASE("TCP auto reconnect recovers after server restart")
{
    messages connections;
    arknet::tcp_server server;
    check(server.start("127.0.0.1", 0), "reconnect server start");
    const auto port = server.get_listen_port();
    arknet::tcp_client client;
    client.set_auto_reconnect(true, 20ms);
    client.bind_connect(
        [&]
        {
            if (!arknet::get_last_error())
                connections.push("connected");
        });
    check(client.start("127.0.0.1", port), "reconnect client connect");
    connections.wait(1);
    server.stop();
    check(server.start("127.0.0.1", port), "reconnect server restart");
    connections.wait(2);
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("TCP queue rejection and callback stop preserve external IO ownership")
{
    io_runner runner;
    messages received;
    arknet::tcp_server server(runner.context);
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(server.start("127.0.0.1", 0), "shared IO server start");
    arknet::tcp_client client(runner.context);
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view data) { received.push(data); });
    check(client.start("127.0.0.1", server.get_listen_port()), "shared IO connect");
    client.set_max_send_buffer_size(4);
    std::atomic<int> bound_rejections{};
    auto reject_before_copy = [&](const arknet::error_code& ec, std::size_t count)
    {
        DOCTEST_CHECK_MESSAGE((ec == asio::error::no_buffer_space && count == 0),
                              "oversized buffer rejected before copy");
        ++bound_rejections;
    };
    check(!client.async_send(asio::const_buffer(nullptr, 5), reject_before_copy), "oversized const_buffer rejection");
    check(!client.async_send("x", 5, reject_before_copy), "oversized pointer/count rejection");
    check(bound_rejections == 2 && client.get_queued_send_buffer_size() == 0, "oversized callback accounting");
    std::promise<arknet::error_code> rejected;
    auto rejected_result = rejected.get_future();
    std::atomic<int> rejection_calls{};
    client.async_send(std::string("too large"),
                      [&](const arknet::error_code& ec, std::size_t count)
                      {
                          ++rejection_calls;
                          DOCTEST_CHECK_MESSAGE(count == 0, "rejected send must not report bytes");
                          rejected.set_value(ec);
                      });
    check(await(rejected_result) == asio::error::no_buffer_space, "queue overflow error");
    check(rejection_calls == 1 && client.get_queued_send_buffer_size() == 0, "overflow callback and queue accounting");
    client.async_send(std::string("ok"));
    check(received.wait(1).front() == "ok", "send after rejection");
    std::promise<arknet::error_code> stopped_from_callback;
    auto stopped_result = stopped_from_callback.get_future();
    client.post(
        [&]
        {
            arknet::clear_last_error();
            const auto pending = client.get_pending_event_count();
            DOCTEST_CHECK_MESSAGE((!arknet::get_last_error() && pending <= 1024), "pending event getter on IO thread");
            client.async_send(std::string("too large"),
                              [&](const arknet::error_code& ec, std::size_t)
                              {
                                  client.stop();
                                  stopped_from_callback.set_value(ec);
                              });
        });
    check(await(stopped_result) == asio::error::no_buffer_space, "stop from rejection callback");
    check(client.wait_stopped(), "owner waits for callback stop");
    check(!runner.context.stopped(), "client must not stop shared IO");
    std::promise<void> still_running;
    auto running_result = still_running.get_future();
    asio::post(runner.context, [&] { still_running.set_value(); });
    await(running_result);
    server.request_stop();
    check(server.wait_stopped(), "server stop request is joinable");
}

DOCTEST_TEST_CASE("Timers replace existing keys and can stop themselves")
{
    arknet::timer timer;
    messages fired;
    std::atomic<int> obsolete_calls{};
    timer.start_timer("replacement", 1s, [&] { ++obsolete_calls; });
    timer.start_timer("replacement", 10ms, 1, [&] { fired.push("replacement"); });
    timer.start_timer("self-stop", 10ms,
                      [&]
                      {
                          timer.stop_timer("self-stop");
                          fired.push("self-stop");
                      });
    const auto callbacks = fired.wait(2);
    check(callbacks.size() == 2 && obsolete_calls == 0, "replaced callback is canceled");
    check(!timer.is_timer_exists("replacement") && !timer.is_timer_exists("self-stop"),
          "completed and self-stopped timer keys removed");
    timer.stop();
}

DOCTEST_TEST_CASE("Send completions accept generic move-only and overloaded callbacks")
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "callback server start");
    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    messages received;
    client.bind_recv([&](std::string_view data) { received.push(data); });
    check(client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram), "callback client connect");
    std::promise<std::pair<arknet::error_code, std::size_t>> completed;
    auto completion = completed.get_future();
    check(client.async_send(
              "owned",
              [promise = std::move(completed), state = std::make_unique<int>(7)](auto error, auto bytes) mutable
              {
                  DOCTEST_CHECK(*state == 7);
                  promise.set_value({error, bytes});
              }),
          "generic move-only completion accepted");
    const auto result = await(completion);
    check(!result.first && result.second == 5, "generic callback reports send result");
    struct overloaded_completion
    {
        std::promise<std::size_t>& promise;
        void operator()(std::size_t bytes) { promise.set_value(bytes); }
        void operator()() { promise.set_value(0); }
    };
    std::promise<std::size_t> overloaded;
    auto overloaded_result = overloaded.get_future();
    check(client.async_send("second", overloaded_completion{overloaded}), "overloaded completion accepted");
    check(await(overloaded_result) == 6, "byte signature selected for overloaded callback");
    const auto messages = received.wait(2);
    check((messages == std::vector<std::string>{"owned", "second"}), "callback sends preserve payloads");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("Blocking sends from another strand on the same context report in_progress")
{
    io_runner runner;
    arknet::tcp_server server(runner.context);
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "cross-strand server start");
    arknet::tcp_client client(runner.context);
    client.set_auto_reconnect(false);
    messages received;
    client.bind_recv([&](std::string_view data) { received.push(data); });
    check(client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram), "cross-strand client connect");
    std::promise<std::pair<arknet::error_code, std::size_t>> submitted;
    auto result = submitted.get_future();
    asio::post(asio::make_strand(runner.context),
               [&]
               {
                   const auto bytes = client.send("across");
                   submitted.set_value({arknet::get_last_error(), bytes});
               });
    const auto admission = await(result);
    check(admission.first == asio::error::in_progress && admission.second == 0,
          "cross-strand send must not block its context");
    check(received.wait(1).front() == "across", "cross-strand send still completes asynchronously");
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("Synchronous timer queries reject cross-strand waits and stopped schedulers")
{
    io_runner runner;
    arknet::timer timer(runner.context);
    timer.start_timer("pending", 1h, [] {});
    check(timer.is_timer_exists("pending"), "timer key exists before query");
    std::promise<std::pair<arknet::error_code, bool>> queried;
    auto result = queried.get_future();
    asio::post(asio::make_strand(runner.context),
               [&]
               {
                   const bool exists = timer.is_timer_exists("pending");
                   queried.set_value({arknet::get_last_error(), exists});
               });
    const auto status = await(result);
    check(status.first == asio::error::operation_not_supported && !status.second,
          "query cannot wait on another strand in its context");
    timer.stop();
    check(!timer.is_timer_exists("pending") && arknet::get_last_error() == asio::error::operation_aborted,
          "stopped timer query returns without waiting");
}

DOCTEST_TEST_CASE("External timer stop drains canceled waits and releases callback state")
{
    io_runner runner;
    arknet::timer timer(runner.context);
    auto retained = std::make_shared<int>(7);
    std::weak_ptr<int> released = retained;
    std::atomic<unsigned> conditions{};
    std::vector<std::future<int>> delayed;
    for (unsigned i = 0; i < 32; ++i)
    {
        timer.start_timer(i, 1h, [retained] {});
        delayed.push_back(timer.post([retained] { return *retained; }, 1h, asio::use_future));
        timer.post_condition_event([retained, &conditions] { ++conditions; });
    }
    auto armed = timer.post([] {}, asio::use_future);
    await(armed);
    retained.reset();
    timer.stop();
    check(conditions == 32, "stop waits for all condition completions");
    check(released.expired(), "stop releases canceled timer and post captures");
    for (auto& future : delayed)
    {
        check(future.wait_for(0s) == std::future_status::ready, "canceled delayed future ready before stop returns");
        bool canceled = false;
        try
        {
            future.get();
        }
        catch (const std::future_error& error)
        {
            canceled = error.code() == std::make_error_code(std::future_errc::broken_promise);
        }
        check(canceled, "canceled delayed task does not invoke its callback");
    }
    check(!runner.context.stopped(), "timer stop preserves external context");
}

DOCTEST_TEST_CASE("Completed future posts release captures before result consumption")
{
    io_runner runner;
    arknet::timer timer(runner.context);
    auto retained = std::make_shared<int>(7);
    std::weak_ptr<int> released = retained;
    std::atomic<int> void_result{};
    int reference_result = 11;
    auto value = timer.post([retained] { return *retained; }, asio::use_future);
    auto empty = timer.dispatch([retained, owned = std::make_unique<int>(3), &void_result]
                                { void_result = *retained + *owned; }, asio::use_future);
    auto reference = timer.post([retained, &reference_result]() -> int& { return reference_result; }, asio::use_future);
    auto delayed = timer.post([retained] { return std::make_unique<int>(*retained); }, 1ms, asio::use_future);
    auto failure =
        timer.post([retained]() -> int { throw std::runtime_error("post callback failure"); }, asio::use_future);
    check(value.wait_for(5s) == std::future_status::ready, "value future completes");
    check(empty.wait_for(5s) == std::future_status::ready, "void future completes");
    check(reference.wait_for(5s) == std::future_status::ready, "reference future completes");
    check(delayed.wait_for(5s) == std::future_status::ready, "move-only result future completes");
    check(failure.wait_for(5s) == std::future_status::ready, "exception future completes");
    retained.reset();
    timer.stop();
    check(released.expired(), "completed futures retain only their results, not callback captures");
    check(value.get() == 7 && void_result == 10, "value result and move-only callback preserved");
    empty.get();
    check(&reference.get() == &reference_result, "reference result preserves identity");
    check(*delayed.get() == 7, "move-only result preserved");
    bool propagated = false;
    try
    {
        failure.get();
    }
    catch (const std::runtime_error& error)
    {
        propagated = std::string_view(error.what()) == "post callback failure";
    }
    check(propagated, "future preserves the callback exception");
    check(!runner.context.stopped(), "future completion preserves external context");
}

DOCTEST_TEST_CASE("Future posts canceled before initialization release captures without consumption")
{
    io_runner runner;
    std::unique_ptr<arknet::timer> timer;
    auto retained = std::make_shared<int>(7);
    std::weak_ptr<int> released = retained;
    std::vector<std::future<int>> canceled;
    std::promise<void> submitted;
    auto submission = submitted.get_future();
    std::atomic<unsigned> unexpected_callbacks{};
    asio::post(runner.context,
               [&, retained = std::move(retained)]() mutable
               {
                   timer = std::make_unique<arknet::timer>(runner.context);
                   auto callback = [retained, &unexpected_callbacks]
                   {
                       ++unexpected_callbacks;
                       return *retained;
                   };
                   canceled.push_back(timer->post(callback, asio::use_future));
                   canceled.push_back(timer->post(std::move(callback), 1h, asio::use_future));
                   retained.reset();
                   timer->stop();
                   submitted.set_value();
               });
    await(submission);
    timer->stop();
    check(released.expired() && unexpected_callbacks == 0,
          "uninitialized futures cannot retain or run canceled callbacks");
    for (auto& future : canceled)
    {
        check(future.wait_for(0s) == std::future_status::ready, "uninitialized canceled future is ready after stop");
        bool abandoned = false;
        try
        {
            future.get();
        }
        catch (const std::future_error& error)
        {
            abandoned = error.code() == std::make_error_code(std::future_errc::broken_promise);
        }
        check(abandoned, "uninitialized task reports broken_promise");
    }
    check(!runner.context.stopped(), "canceling initialization preserves external context");
}

DOCTEST_TEST_CASE("Pool and queued-event futures release captures before result consumption")
{
    auto pool_capture = std::make_shared<int>(7);
    std::weak_ptr<int> pool_released = pool_capture;
    arknet::io_pool pool(1);
    check(pool.start(), "future pool starts");
    auto pooled = pool.post([pool_capture] { return *pool_capture; });
    check(pooled.wait_for(5s) == std::future_status::ready, "pool task completes");
    pool_capture.reset();
    pool.stop();
    check(pool_released.expired(), "stopped pool releases the callback while its result is unconsumed");
    check(pooled.get() == 7, "pool result preserved");

    io_runner runner;
    auto queue_capture = std::make_shared<int>(11);
    std::weak_ptr<int> queue_released = queue_capture;
    arknet::tcp_server server(runner.context);
    check(server.start("127.0.0.1", 0), "future queue server starts");
    arknet::tcp_client client(runner.context);
    client.set_auto_reconnect(false);
    check(client.start("127.0.0.1", server.get_listen_port()), "future queue client connects");
    auto queued = client.post_queued_event([queue_capture] { return *queue_capture; }, asio::use_future);
    check(queued.wait_for(5s) == std::future_status::ready, "queued task completes");
    queue_capture.reset();
    client.stop();
    server.stop();
    check(queue_released.expired(), "stopped queue releases the callback while its result is unconsumed");
    check(queued.get() == 11, "queued result preserved");
    check(!runner.context.stopped(), "queued task preserves external context");
}

struct callback_lifetime_probe
{
    explicit callback_lifetime_probe(std::promise<void>& result) : released(result) {}
    std::promise<void>& released;
    ~callback_lifetime_probe() { released.set_value(); }
};

DOCTEST_TEST_CASE("External timers can be destroyed from their own IO callback")
{
    io_runner runner;
    auto timer = std::make_unique<arknet::timer>(runner.context);
    std::promise<void> released, destroyed;
    auto release = released.get_future();
    auto destruction = destroyed.get_future();
    auto retained = std::make_shared<callback_lifetime_probe>(released);
    std::atomic<unsigned> unexpected_callbacks{};
    timer->start_timer("pending", 1h, [retained, &unexpected_callbacks] { ++unexpected_callbacks; });
    timer->post([retained, &unexpected_callbacks] { ++unexpected_callbacks; }, 1h);
    timer->post_condition_event([retained, &unexpected_callbacks] { ++unexpected_callbacks; });
    retained.reset();
    timer->post(
        [&]
        {
            timer.reset();
            destroyed.set_value();
        });
    await(destruction);
    await(release);
    check(unexpected_callbacks == 0, "destruction suppresses late user callbacks");
    check(!runner.context.stopped(), "destruction preserves external runner");
}

DOCTEST_TEST_CASE("External timers can be destroyed before queued waits initialize")
{
    io_runner runner;
    std::promise<void> released, destroyed;
    auto release = released.get_future();
    auto destruction = destroyed.get_future();
    auto retained = std::make_shared<callback_lifetime_probe>(released);
    std::atomic<unsigned> unexpected_callbacks{};
    asio::post(asio::make_strand(runner.context),
               [&, retained = std::move(retained)]() mutable
               {
                   auto timer = std::make_unique<arknet::timer>(runner.context);
                   timer->start_timer("queued", 1h, [retained, &unexpected_callbacks] { ++unexpected_callbacks; });
                   timer->post([retained, &unexpected_callbacks] { ++unexpected_callbacks; }, 1h);
                   timer->post_condition_event([retained, &unexpected_callbacks] { ++unexpected_callbacks; });
                   retained.reset();
                   timer.reset();
                   destroyed.set_value();
               });
    await(destruction);
    await(release);
    check(unexpected_callbacks == 0, "queued initialization survives owner destruction");
}

DOCTEST_TEST_CASE("Stopping from another IO strand cancels waits that have not initialized")
{
    io_runner runner;
    std::unique_ptr<arknet::timer> timer;
    std::promise<void> stopped, released;
    auto stop = stopped.get_future();
    auto release = released.get_future();
    auto retained = std::make_shared<callback_lifetime_probe>(released);
    std::atomic<unsigned> unexpected_callbacks{};
    asio::post(asio::make_strand(runner.context),
               [&, retained = std::move(retained)]() mutable
               {
                   timer = std::make_unique<arknet::timer>(runner.context);
                   timer->start_timer("queued", 1h, [retained, &unexpected_callbacks] { ++unexpected_callbacks; });
                   timer->post([retained, &unexpected_callbacks] { ++unexpected_callbacks; }, 1h);
                   timer->post_condition_event([retained, &unexpected_callbacks] { ++unexpected_callbacks; });
                   retained.reset();
                   timer->stop();
                   stopped.set_value();
               });
    await(stop);
    timer->stop();
    await(release);
    check(unexpected_callbacks == 0, "stop suppresses queued initialization before cancellation runs");
    timer.reset();
    check(!runner.context.stopped(), "queued stop preserves external context");
}

DOCTEST_TEST_CASE("Timer queries reject a directly stopped external context")
{
    asio::io_context context;
    unsigned unexpected_callbacks = 0;
    {
        arknet::timer timer(context);
        timer.start_timer("queued", 1h, [&] { ++unexpected_callbacks; });
        context.stop();
        check(!timer.is_timer_exists("queued") && arknet::get_last_error() == asio::error::operation_aborted,
              "stopped context query cannot wait");
        timer.stop();
    }
    context.restart();
    context.run();
    check(unexpected_callbacks == 0, "restarting context after timer destruction is safe");
}
