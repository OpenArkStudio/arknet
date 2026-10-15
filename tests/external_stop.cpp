// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <atomic>
#include <future>
#include <thread>

#include <arknet/tcp/tcp_client.hpp>
#include <arknet/tcp/tcp_server.hpp>
#include <arknet/tcp/tcps_server.hpp>
#include "check.hpp"

namespace {

struct io_runner
{
    asio::io_context context;
    asio::executor_work_guard<asio::io_context::executor_type> work{context.get_executor()};
    std::thread worker{[this] { context.run(); }};
    std::thread second{[this] { context.run(); }};

    ~io_runner() {
        work.reset();
        context.stop();
        worker.join();
        second.join();
    }
};

struct retirement
{
    std::atomic<unsigned> created{}, destroyed{}, canceled_accepts{}, callbacks{};
    std::atomic<bool> destructor_on_lane{};
    std::promise<void> entered;
    std::promise<void> stopped;
    std::promise<void> pin_acquired;
    std::promise<void> release_pin;
    std::promise<void> release;
    std::shared_future<void> pin_released{release_pin.get_future().share()};
    std::shared_future<void> released{release.get_future().share()};
};

struct observed_session : arknet::tcp_session_t<observed_session>
{
    using base = arknet::tcp_session_t<observed_session>;
    using base::base;
    std::shared_ptr<retirement> observed;
    bool pause_after_stop = false;
    bool pause_in_destructor = false;
    bool pause_waiter = false;
    bool stop_before_complete = false;
    std::function<void()> destructor_check;

    ~observed_session() {
        if (observed)
        {
            if (pause_in_destructor)
            {
                observed->destructor_on_lane = running_in_this_thread();
                observed->entered.set_value();
                if (destructor_check)
                {
                    destructor_check();
                }
                observed->released.wait();
                ++observed->callbacks;
            }
            ++observed->destroyed;
        }
    }

    void wait_retired() {
        base::_wait_async_operations();
    }

    void _wait_async_operations() {
        base::_wait_async_operations();
        if (pause_waiter)
        {
            observed->pin_acquired.set_value();
            observed->pin_released.wait();
        }
    }

    template<class Condition, class Chain>
    void _complete_connect(arknet::error_code error, std::shared_ptr<observed_session> owner,
        std::shared_ptr<arknet::detail::ecs_t<Condition>> policy, Chain chain) {
        if (stop_before_complete)
        {
            stop();
            observed->entered.set_value();
        }
        base::_complete_connect(error, std::move(owner), std::move(policy), std::move(chain));
    }

    template<class Chain>
    void _handle_stop(const arknet::error_code& error, std::shared_ptr<observed_session> owner, Chain chain) {
        base::_handle_stop(error, std::move(owner), std::move(chain));
        if (pause_in_destructor)
        {
            observed->stopped.set_value();
        }
        if (pause_after_stop)
        {
            // The session counter has already retired on this lane. Its queued
            // continuation must still keep the parent listener alive.
            post([state = observed] {
                state->entered.set_value();
                state->released.wait();
                ++state->callbacks;
            });
        }
    }
};

struct observed_server : arknet::tcp_server_impl_t<observed_server, observed_session>
{
    using base = arknet::tcp_server_impl_t<observed_server, observed_session>;
    using base::base;
    std::shared_ptr<retirement> observed;

    std::shared_ptr<observed_session> _make_session() {
        auto session = base::_make_session();
        session->observed = observed;
        ++observed->created;
        return session;
    }

    template<class Condition>
    void _handle_accept(const arknet::error_code& error, std::shared_ptr<observed_session> session,
        std::shared_ptr<observed_server> owner, std::shared_ptr<arknet::detail::ecs_t<Condition>> policy) {
        if (error == asio::error::operation_aborted)
        {
            ++observed->canceled_accepts;
        }
        base::_handle_accept(error, std::move(session), std::move(owner), std::move(policy));
    }
};

struct allocator_probe
{
    bool fail = false;
    unsigned allocations = 0;
};

template<class T>
struct tagged_allocator
{
    using value_type = T;
    allocator_probe* state;

    explicit tagged_allocator(allocator_probe& state)
        : state(&state) {}

    template<class U>
    tagged_allocator(const tagged_allocator<U>& other)
        : state(other.state) {}

    T* allocate(std::size_t count) {
        ++state->allocations;
        if (state->fail)
        {
            throw std::bad_alloc();
        }
        return std::allocator<T>{}.allocate(count);
    }

    void deallocate(T* data, std::size_t count) {
        std::allocator<T>{}.deallocate(data, count);
    }

    template<class U>
    bool operator==(const tagged_allocator<U>& other) const noexcept {
        return state == other.state;
    }
};

} // namespace

DOCTEST_TEST_CASE("External TCP owner stop retires canceled native accepts before return and supports restart") {
    io_runner runner;
    auto observed = std::make_shared<retirement>();
    observed_server server(runner.context);
    server.observed = observed;
    for (unsigned generation = 1; generation <= 32; ++generation)
    {
        check(server.start("127.0.0.1", 0), "listener starts with a pending native accept");
        auto armed = server.post([] {}, asio::use_future);
        await(armed);
        DOCTEST_CHECK(observed->created == generation);
        server.stop();
        DOCTEST_CHECK(observed->canceled_accepts == generation);
        DOCTEST_CHECK(observed->destroyed == generation);
        DOCTEST_CHECK_FALSE(runner.context.stopped());
    }
}

DOCTEST_TEST_CASE("External TCP owner stop waits for stopped session continuations on another context") {
    io_runner listener_runner, session_runner;
    auto observed = std::make_shared<retirement>();
    auto entered = observed->entered.get_future();
    std::promise<void> logical_stop;
    auto logically_stopped = logical_stop.get_future();
    std::promise<std::shared_ptr<observed_session>> accepted;
    auto accepted_session = accepted.get_future();
    std::array<asio::io_context*, 2> contexts{&listener_runner.context, &session_runner.context};
    observed_server server(contexts);
    server.observed = observed;
    server.bind_accept([&](auto& session) {
        session->pause_after_stop = true;
        accepted.set_value(session);
    });
    server.bind_stop([&] { logical_stop.set_value(); });
    check(server.start("127.0.0.1", 0), "two-context listener starts");
    asio::io_context peer_context;
    asio::ip::tcp::socket peer(peer_context);
    peer.connect({asio::ip::address_v4::loopback(), server.get_listen_port()});
    auto retained_session = await(accepted_session);
    arknet::error_code ignored;
    peer.close(ignored);
    auto stopping = std::async(std::launch::async, [&] { server.stop(); });
    await(entered);
    await(logically_stopped);
    const auto before_release = stopping.wait_for(100ms);
    // Release before asserting so a negative run can unwind without deadlock.
    observed->release.set_value();
    await(stopping);
    DOCTEST_CHECK(before_release == std::future_status::timeout);
    DOCTEST_CHECK(observed->callbacks == 1);
    DOCTEST_CHECK(retained_session->is_stopped());
    DOCTEST_CHECK_FALSE(listener_runner.context.stopped());
    DOCTEST_CHECK_FALSE(session_runner.context.stopped());
}

DOCTEST_TEST_CASE("External TCP stop retires user timer, delayed post and condition captures") {
    io_runner runner;
    std::atomic<unsigned> conditions{};
    arknet::tcp_server server(runner.context);
    arknet::tcp_client client(runner.context);
    client.set_auto_reconnect(false);
    check(server.start("127.0.0.1", 0), "capture listener starts");
    check(client.start("127.0.0.1", server.get_listen_port()), "capture client starts");
    auto captured = std::make_shared<int>(7);
    std::weak_ptr<int> released = captured;
    std::vector<std::future<void>> posts;
    for (unsigned index = 0; index < 32; ++index)
    {
        server.start_timer(index, 1h, [captured] {});
        client.start_timer(index, 1h, [captured] {});
        posts.push_back(server.post([captured] {}, 1h, asio::use_future));
        posts.push_back(client.post([captured] {}, 1h, asio::use_future));
        server.post_condition_event([captured, &conditions] { ++conditions; });
        client.post_condition_event([captured, &conditions] { ++conditions; });
    }
    auto server_armed = server.post([] {}, asio::use_future);
    auto client_armed = client.post([] {}, asio::use_future);
    await(server_armed);
    await(client_armed);
    captured.reset();
    client.stop();
    server.stop();
    DOCTEST_CHECK(released.expired());
    DOCTEST_CHECK(conditions == 64);
    for (auto& post : posts)
    {
        DOCTEST_CHECK(post.wait_for(0s) == std::future_status::ready);
    }
}

DOCTEST_TEST_CASE("TCP stop retires condition captures rejected before initialization") {
    io_runner runner;
    arknet::tcp_server server(runner.context);
    check(server.start("127.0.0.1", 0), "condition initialization listener starts");
    std::promise<void> entered, release, requested;
    auto blocked = entered.get_future();
    auto open = release.get_future().share();
    auto stopped_on_worker = requested.get_future();
    server.post([&] {
        entered.set_value();
        open.wait();
    });
    await(blocked);
    std::atomic<unsigned> callbacks{};
    auto captured = std::make_shared<int>(7);
    std::weak_ptr<int> retired = captured;
    server.post_condition_event([captured, &callbacks] { ++callbacks; });
    captured.reset();
    asio::post(runner.context, [&] {
        server.stop();
        requested.set_value();
    });
    const auto status = stopped_on_worker.wait_for(5s);
    release.set_value();
    await(stopped_on_worker);
    server.stop();
    DOCTEST_CHECK(status == std::future_status::ready);
    DOCTEST_CHECK(callbacks == 0);
    DOCTEST_CHECK(retired.expired());
}

DOCTEST_TEST_CASE("TCP stop from a different strand in the same context does not wait and preserves sibling traffic") {
    io_runner runner;
    messages received;
    std::promise<void> requested;
    auto request = requested.get_future();
    arknet::tcp_server server(runner.context), canceled(runner.context);
    arknet::tcp_client client(runner.context);
    server.bind_recv([](auto& session, std::string_view bytes) { session->async_send(bytes); });
    client.bind_recv([&](std::string_view bytes) { received.push(bytes); });
    client.set_auto_reconnect(false);
    check(server.start("127.0.0.1", 0, arknet::use_dgram), "sibling listener starts");
    check(canceled.start("127.0.0.1", 0), "canceled listener starts");
    check(client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram), "sibling client starts");
    server.post([&] {
        canceled.stop();
        requested.set_value();
    });
    await(request);
    canceled.stop();
    client.async_send("still running");
    DOCTEST_CHECK(received.wait(1).front() == "still running");
    DOCTEST_CHECK_FALSE(runner.context.stopped());
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("TCP condition cancellation cannot arm new long waits during owner stop and restart reopens waits") {
    io_runner runner;
    std::atomic<unsigned> immediate{}, fired{};
    auto captured = std::make_shared<int>(7);
    std::weak_ptr<int> released = captured;
    std::promise<std::future<void>> late;
    auto late_post = late.get_future();
    arknet::tcp_server server(runner.context);
    check(server.start("127.0.0.1", 0), "late-wait listener starts");
    server.post_condition_event([&, captured] {
        server.start_timer("late", 1h, [captured] {});
        late.set_value(server.post([captured] {}, 1h, asio::use_future));
        server.post([&] { ++immediate; });
    });
    auto armed = server.post([] {}, asio::use_future);
    await(armed);
    captured.reset();
    server.stop();
    auto canceled = await(late_post);
    DOCTEST_CHECK(canceled.wait_for(0s) == std::future_status::ready);
    DOCTEST_CHECK(released.expired());
    DOCTEST_CHECK(immediate == 1);
    check(server.start("127.0.0.1", 0), "explicit restart reopens waits");
    std::promise<void> timer;
    auto timer_fired = timer.get_future();
    server.start_timer("restarted", 1ms, 1, [&] {
        ++fired;
        timer.set_value();
    });
    auto delayed = server.post([&] { ++fired; }, 1ms, asio::use_future);
    std::promise<void> condition;
    auto condition_fired = condition.get_future();
    auto event = server.post_condition_event([&] {
        ++fired;
        condition.set_value();
    });
    event->notify();
    await(timer_fired);
    await(delayed);
    await(condition_fired);
    DOCTEST_CHECK(fired == 3);
    server.stop();
}

DOCTEST_TEST_CASE("Concurrent TCP owner stops independently wait for retained sessions") {
    io_runner runner;
    auto observed = std::make_shared<retirement>();
    std::promise<std::shared_ptr<observed_session>> accepted;
    auto accepted_session = accepted.get_future();
    std::promise<void> release;
    auto begin = release.get_future().share();
    observed_server server(runner.context);
    server.observed = observed;
    server.bind_accept([&](auto& session) { accepted.set_value(session); });
    check(server.start("127.0.0.1", 0), "concurrent-stop listener starts");
    asio::io_context peer_context;
    asio::ip::tcp::socket peer(peer_context);
    peer.connect({asio::ip::address_v4::loopback(), server.get_listen_port()});
    auto retained = await(accepted_session);
    arknet::error_code ignored;
    peer.close(ignored);
    auto first = std::async(std::launch::async, [&] {
        begin.wait();
        server.stop();
    });
    auto second = std::async(std::launch::async, [&] {
        begin.wait();
        server.stop();
    });
    release.set_value();
    await(first);
    await(second);
    DOCTEST_CHECK(retained->is_stopped());
    DOCTEST_CHECK(observed->canceled_accepts == 1);
    DOCTEST_CHECK_FALSE(runner.context.stopped());
}

DOCTEST_TEST_CASE("Concurrent external TCP owner stops each wait for the final session destructor") {
    io_runner listener, sessions;
    std::array<asio::io_context*, 2> contexts{&listener.context, &sessions.context};
    auto observed = std::make_shared<retirement>();
    auto entered = observed->entered.get_future();
    auto stopped = observed->stopped.get_future();
    std::promise<std::shared_ptr<observed_session>> accepted;
    auto accepted_session = accepted.get_future();
    observed_server server(contexts);
    server.observed = observed;
    server.bind_accept([&](auto& session) {
        session->pause_in_destructor = true;
        accepted.set_value(session);
    });
    check(server.start("127.0.0.1", 0), "destructor listener starts");
    asio::io_context peer_context;
    asio::ip::tcp::socket peer(peer_context);
    peer.connect({asio::ip::address_v4::loopback(), server.get_listen_port()});
    auto retained = await(accepted_session);
    arknet::error_code ignored;
    peer.close(ignored);
    await(stopped);
    retained->wait_retired();
    std::promise<void> body_entered, release_body;
    auto body = body_entered.get_future();
    auto open = release_body.get_future().share();
    retained->post([&] {
        body_entered.set_value();
        open.wait();
    });
    await(body);
    std::weak_ptr<observed_session> expired = retained;
    retained.reset();
    release_body.set_value();
    await(entered);
    DOCTEST_CHECK(expired.expired());
    DOCTEST_CHECK(observed->destructor_on_lane);
    auto first = std::async(std::launch::async, [&] { server.stop(); });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (server.iopool().started() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    const bool pool_closing = !server.iopool().started();
    auto second = std::async(std::launch::async, [&] { server.stop(); });
    const auto first_status = first.wait_for(100ms);
    const auto second_status = second.wait_for(100ms);
    observed->release.set_value();
    await(first);
    await(second);
    DOCTEST_CHECK(pool_closing);
    DOCTEST_CHECK(first_status == std::future_status::timeout);
    DOCTEST_CHECK(second_status == std::future_status::timeout);
    DOCTEST_CHECK(observed->callbacks == 1);
}

DOCTEST_TEST_CASE("Concurrent TCP owner stops wait when a draining waiter owns the final session") {
    io_runner listener, sessions;
    std::array<asio::io_context*, 2> contexts{&listener.context, &sessions.context};
    auto observed = std::make_shared<retirement>();
    auto entered = observed->entered.get_future();
    auto stopped = observed->stopped.get_future();
    auto pinned = observed->pin_acquired.get_future();
    std::promise<void> reentered;
    auto reentry = reentered.get_future();
    bool stop_reentry_rejected = false, wait_reentry_rejected = false;
    std::promise<std::shared_ptr<observed_session>> accepted;
    auto accepted_session = accepted.get_future();
    observed_server server(contexts);
    server.observed = observed;
    server.bind_accept([&](auto& session) {
        session->pause_in_destructor = true;
        accepted.set_value(session);
    });
    check(server.start("127.0.0.1", 0), "waiter-owned destructor listener starts");
    asio::io_context peer_context;
    asio::ip::tcp::socket peer(peer_context);
    peer.connect({asio::ip::address_v4::loopback(), server.get_listen_port()});
    auto retained = await(accepted_session);
    arknet::error_code ignored;
    peer.close(ignored);
    await(stopped);
    retained->wait_retired();
    retained->destructor_check = [&] {
        server.stop();
        stop_reentry_rejected = arknet::get_last_error() == asio::error::operation_not_supported;
        wait_reentry_rejected =
            !server.wait_stopped() && arknet::get_last_error() == asio::error::operation_not_supported;
        reentered.set_value();
    };
    retained->pause_waiter = true;
    auto first = std::async(std::launch::async, [&] { server.stop(); });
    await(pinned);
    std::weak_ptr<observed_session> expired = retained;
    retained.reset();
    observed->release_pin.set_value();
    await(entered);
    await(reentry);
    const bool pool_still_active = server.iopool().started();
    auto second = std::async(std::launch::async, [&] { server.stop(); });
    const auto first_status = first.wait_for(100ms);
    const auto second_status = second.wait_for(100ms);
    observed->release.set_value();
    await(first);
    await(second);
    DOCTEST_CHECK(expired.expired());
    DOCTEST_CHECK_FALSE(observed->destructor_on_lane);
    DOCTEST_CHECK(pool_still_active);
    DOCTEST_CHECK(first_status == std::future_status::timeout);
    DOCTEST_CHECK(second_status == std::future_status::timeout);
    DOCTEST_CHECK(stop_reentry_rejected);
    DOCTEST_CHECK(wait_reentry_rejected);
}

DOCTEST_TEST_CASE("TCP stop after the socket snapshot rejects a successful session connection") {
    io_runner listener, sessions;
    std::array<asio::io_context*, 2> contexts{&listener.context, &sessions.context};
    auto observed = std::make_shared<retirement>();
    auto entered = observed->entered.get_future();
    std::atomic<unsigned> connected{};
    observed_server server(contexts);
    server.observed = observed;
    server.bind_accept([](auto& session) { session->stop_before_complete = true; });
    server.bind_connect([&](auto&) { ++connected; });
    check(server.start("127.0.0.1", 0), "socket-snapshot listener starts");
    asio::io_context peer_context;
    asio::ip::tcp::socket peer(peer_context);
    peer.connect({asio::ip::address_v4::loopback(), server.get_listen_port()});
    await(entered);
    server.stop();
    DOCTEST_CHECK(connected == 0);
}

DOCTEST_TEST_CASE("Ordinary TCP reconnect keeps user timers available") {
    io_runner runner;
    std::atomic<unsigned> connects{};
    std::promise<void> reconnected, timer;
    auto reconnect = reconnected.get_future();
    auto fired = timer.get_future();
    arknet::tcp_server server(runner.context);
    arknet::tcp_client client(runner.context);
    client.set_auto_reconnect(true, 1ms);
    client.bind_connect([&] {
        if (!arknet::get_last_error() && ++connects == 2)
        {
            reconnected.set_value();
        }
    });
    check(server.start("127.0.0.1", 0), "reconnect listener starts");
    const auto port = server.get_listen_port();
    check(client.start("127.0.0.1", port), "reconnecting client starts");
    server.stop();
    check(server.start("127.0.0.1", port), "listener restarts on the same port");
    await(reconnect);
    client.start_timer("after reconnect", 1ms, 1, [&] { timer.set_value(); });
    await(fired);
    client.stop();
    server.stop();
}

DOCTEST_TEST_CASE("Native retirement wrapper preserves explicit associations and native executor fallbacks") {
    asio::io_context context;
    auto fallback = asio::make_strand(context);
    auto bound = asio::make_strand(context);
    asio::cancellation_signal fallback_signal, bound_signal;
    allocator_probe fallback_allocator, bound_allocator;
    auto pending = std::make_shared<std::atomic_size_t>(0);
    auto callback = [] {};
    auto plain = arknet::detail::pending_handler(pending, callback);
    DOCTEST_CHECK(asio::get_associated_executor(plain, fallback) == fallback);
    DOCTEST_CHECK(
        asio::get_associated_allocator(plain, tagged_allocator<int>(fallback_allocator)).state == &fallback_allocator);
    DOCTEST_CHECK(asio::get_associated_cancellation_slot(plain, fallback_signal.slot()) == fallback_signal.slot());
    DOCTEST_CHECK(asio::get_associated_immediate_executor(plain, fallback) ==
                  asio::get_associated_immediate_executor(callback, fallback));
    auto specified = asio::bind_executor(
        bound, asio::bind_allocator(tagged_allocator<int>(bound_allocator),
                   asio::bind_cancellation_slot(bound_signal.slot(), asio::bind_immediate_executor(bound, callback))));
    auto tracked = arknet::detail::pending_handler(pending, std::move(specified));
    DOCTEST_CHECK(asio::get_associated_executor(tracked, fallback) == bound);
    DOCTEST_CHECK(
        asio::get_associated_allocator(tracked, tagged_allocator<int>(fallback_allocator)).state == &bound_allocator);
    DOCTEST_CHECK(asio::get_associated_cancellation_slot(tracked, fallback_signal.slot()) == bound_signal.slot());
    DOCTEST_CHECK(asio::get_associated_immediate_executor(tracked, fallback) == bound);
}

DOCTEST_TEST_CASE("Native retirement token outlives captures and rolls back failed initiation") {
    struct capture_probe
    {
        std::shared_ptr<std::atomic_size_t> pending;
        std::atomic_size_t& observed;

        ~capture_probe() {
            observed.store(pending->load());
        }
    };

    auto pending = std::make_shared<std::atomic_size_t>(0);
    std::atomic_size_t observed{};
    auto capture = std::make_shared<capture_probe>(pending, observed);
    std::weak_ptr<capture_probe> released = capture;
    {
        auto tracked = arknet::detail::pending_handler(pending, [capture] {});
        capture.reset();
        // An initialization handler may reject the callback and return with
        // its captures intact. They must still retire before the count does.
        tracked();
        DOCTEST_CHECK(observed == 0);
        DOCTEST_CHECK(pending->load() == 1);
    }
    DOCTEST_CHECK(released.expired());
    DOCTEST_CHECK(observed == 1);
    DOCTEST_CHECK(pending->load() == 0);
    asio::io_context context;
    asio::steady_timer timer(context);
    timer.expires_after(1h);
    allocator_probe failure{true};
    bool threw = false;
    try
    {
        timer.async_wait(asio::bind_allocator(
            tagged_allocator<int>(failure), arknet::detail::pending_handler(pending, [](arknet::error_code) {})));
    }
    catch (const std::bad_alloc&)
    { threw = true; }
    DOCTEST_CHECK(threw);
    DOCTEST_CHECK(failure.allocations == 1);
    DOCTEST_CHECK(pending->load() == 0);
}

#if defined(ARKNET_ENABLE_SSL)
namespace {

struct silent_tls_session : arknet::tcps_session_t<silent_tls_session>
{
    using base = arknet::tcps_session_t<silent_tls_session>;
    using base::base;
    std::function<void()> handshake_started;
    std::function<void(bool)> starting_canceled;

    silent_tls_session& set_linger(bool enabled, int seconds) noexcept {
        if (starting_canceled)
        {
            starting_canceled(running_in_this_thread());
        }
        return base::set_linger(enabled, seconds);
    }

    template<class Condition, class Chain>
    void _post_handshake(std::shared_ptr<silent_tls_session> owner,
        std::shared_ptr<arknet::detail::ecs_t<Condition>> policy, Chain chain) {
        base::_post_handshake(std::move(owner), std::move(policy), std::move(chain));
        handshake_started();
    }
};

template<class Server>
void stop_silent_handshake(Server& server) {
    std::promise<void> started;
    auto handshake = started.get_future();
    std::atomic<unsigned> completed{};
    std::atomic<unsigned> canceled_on_lane{}, canceled_off_lane{};
    server.set_cert_file("", std::string(ARKNET_TEST_CERT_DIR) + "/server.pem",
        std::string(ARKNET_TEST_CERT_DIR) + "/server-key.pem", "");
    check(!arknet::get_last_error(), "silent-peer TLS identity loads");
    server.bind_accept([&](auto& session) {
        session->set_connect_timeout(30s);
        session->handshake_started = [&] { started.set_value(); };
        session->starting_canceled = [&](bool on_lane) { ++(on_lane ? canceled_on_lane : canceled_off_lane); };
    });
    server.bind_handshake([&](auto&) { ++completed; });
    check(server.start("127.0.0.1", 0), "TLS listener starts");
    asio::io_context peer_context;
    asio::ip::tcp::socket peer(peer_context);
    peer.connect({asio::ip::address_v4::loopback(), server.get_listen_port()});
    await(handshake);
    DOCTEST_CHECK(server.get_session_count() == 0);
    auto stopping = std::async(std::launch::async, [&] { server.stop(); });
    const auto status = stopping.wait_for(5s);
    // Rescue the old-code negative without waiting for its 30-second deadline.
    arknet::error_code ignored;
    peer.close(ignored);
    await(stopping);
    DOCTEST_CHECK(status == std::future_status::ready);
    DOCTEST_CHECK(completed == 1);
    DOCTEST_CHECK(canceled_on_lane == 1);
    DOCTEST_CHECK(canceled_off_lane == 0);
}

} // namespace

DOCTEST_TEST_CASE("Owned TLS server stop cancels a silent handshake before session registration") {
    arknet::tcps_server_t<silent_tls_session> server(std::size_t{2});
    stop_silent_handshake(server);
}

DOCTEST_TEST_CASE("External TLS server stop cancels a silent handshake before session registration") {
    io_runner listener, sessions;
    std::array<asio::io_context*, 2> contexts{&listener.context, &sessions.context};
    arknet::tcps_server_t<silent_tls_session> server(contexts);
    stop_silent_handshake(server);
    DOCTEST_CHECK_FALSE(listener.context.stopped());
    DOCTEST_CHECK_FALSE(sessions.context.stopped());
}
#endif
