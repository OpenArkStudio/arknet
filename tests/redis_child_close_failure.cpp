// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#if defined(_MSC_VER)
#include "scoped_asio_allocation_failure.hpp"

#endif

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <latch>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arknet/redis/pool.hpp>
#include "redis_test_peer.hpp"
#include "scoped_allocation_failure.hpp"

namespace {

using namespace redis_test;

struct observations
{
    std::atomic<unsigned> callbacks{}, captures{}, transports{}, constructed{}, stop_entries{}, stop_failures{};
};

observations observed;
thread_local unsigned inject_on_stop{};

class observed_transport : public arknet::tcp_client
{
public:
    template<class... Args>
    explicit observed_transport(Args&&... args)
        : arknet::tcp_client(std::forward<Args>(args)...) {
        ++observed.constructed;
    }

    inline static std::mutex callbacks_mutex;
    inline static std::function<void()> before_connect;

    template<class Handler>
    auto& bind_connect(Handler handler) {
        arknet::tcp_client::bind_connect([handler = std::move(handler)]() mutable {
            const auto error = arknet::get_last_error();
            std::function<void()> callback;
            {
                std::lock_guard lock(callbacks_mutex);
                callback = std::exchange(before_connect, {});
            }
            if (callback)
            {
                callback();
            }
            arknet::set_last_error(error);
            handler();
        });
        return *this;
    }

    void stop() {
        ++observed.stop_entries;
        if (inject_on_stop && !--inject_on_stop)
        {
            scoped_allocation_failure::arm();
        }
        try
        { arknet::tcp_client::stop(); }
        catch (...)
        {
            ++observed.stop_failures;
            throw;
        }
    }

    ~observed_transport() {
        ++observed.transports;
    }
};

using observed_client = arknet::redis::basic_client<observed_transport>;
using observed_pool = arknet::redis::basic_pool<observed_transport>;

struct capture_probe
{
    ~capture_probe() {
        ++observed.captures;
    }
};

void reset_observations() {
    observed.callbacks = 0;
    observed.captures = 0;
    observed.transports = 0;
    observed.constructed = 0;
    observed.stop_entries = 0;
    observed.stop_failures = 0;
    std::lock_guard lock(observed_transport::callbacks_mutex);
    observed_transport::before_connect = {};
}

template<class Endpoint>
auto retirement_guard(Endpoint& endpoint) {
    auto retire = [](Endpoint* owner) noexcept {
        try
        {
            if (!owner->close_and_wait())
            {
                std::terminate();
            }
        }
        catch (...)
        { std::terminate(); }
    };
    return std::unique_ptr<Endpoint, decltype(retire)>(&endpoint, retire);
}

template<class Endpoint>
void close_inline(Endpoint& endpoint, unsigned fault) {
    inject_on_stop = fault;
    endpoint.close();
    inject_on_stop = 0;
    scoped_allocation_failure::disarm();
}

void check_retirement(bool fault, std::size_t injections, unsigned count = 1) {
    CHECK(scoped_allocation_failure::injections == injections + unsigned(fault));
    CHECK(observed.stop_entries >= count);
    CHECK(observed.callbacks == count);
    CHECK(observed.captures == count);
    CHECK(observed.transports == count);
}

template<class Endpoint>
void close_concurrently(Endpoint& endpoint) {
    std::array<bool, 2> completed{};
    std::array<std::exception_ptr, 2> failures{};
    std::latch start{1};
    std::vector<std::jthread> owners;
    for (std::size_t index{}; index != completed.size(); ++index)
    {
        owners.emplace_back([&, index] {
            start.wait();
            try
            { completed[index] = endpoint.close_and_wait(); }
            catch (...)
            { failures[index] = std::current_exception(); }
        });
    }
    start.count_down();
    owners.clear();
    for (std::size_t index{}; index != completed.size(); ++index)
    {
        CHECK_FALSE(failures[index]);
        CHECK(completed[index]);
    }
}

void pending_connect(bool fault) {
    reset_observations();
    std::promise<void> received;
    auto hello = received.get_future();
    server endpoint([&](peer& remote) {
        remote.expect({"HELLO", "3"});
        received.set_value();
        remote.wait_closed();
    });
    arknet::redis::client_options options;
    options.connect_timeout = 30s;
    observed_client connection(options);
    std::promise<result> completion;
    auto ready = completion.get_future();
    std::promise<void> closed;
    auto close_done = closed.get_future();
    auto capture = std::make_shared<capture_probe>();
    auto retirement = retirement_guard(connection);
    arknet::redis::connect_options target;
    target.port = endpoint.port();
    connection.async_connect(target, [&, capture](auto response) {
        ++observed.callbacks;
        connection.close();
        CHECK_FALSE(connection.close_and_wait());
        CHECK(arknet::get_last_error() == asio::error::operation_not_supported);
        completion.set_value(std::move(response));
    });
    capture.reset();
    REQUIRE(hello.wait_for(5s) == std::future_status::ready);
    const auto injections = scoped_allocation_failure::injections.load();
    connection.async_execute({"PING"}, [&](auto response) {
        CHECK(response.error == client_errc::not_connected);
        close_inline(connection, unsigned(fault));
        closed.set_value();
    });
    REQUIRE(close_done.wait_for(5s) == std::future_status::ready);
    close_done.get();
    close_concurrently(connection);
    CHECK(connection.close_and_wait());
    check_retirement(fault, injections);
    if (ready.wait_for(0ms) == std::future_status::ready)
    {
        const auto response = ready.get();
        CHECK(response.error == client_errc::closed);
        CHECK_FALSE(response.command_may_have_run);
    }
    else
    {
        FAIL_CHECK("Accepted connect callback was not terminalized before permanent close returned");
    }
    endpoint.finish();
}

void pending_command(bool fault) {
    reset_observations();
    std::promise<void> received;
    auto sent = received.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"ECHO", "held"});
        received.set_value();
        remote.wait_closed();
    });
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    observed_pool connections(options);
    std::promise<observed_pool::acquire_result> acquired;
    auto initial = acquired.get_future();
    std::promise<result> completion;
    auto ready = completion.get_future();
    std::promise<void> closed;
    auto close_done = closed.get_future();
    auto capture = std::make_shared<capture_probe>();
    auto retirement = retirement_guard(connections);
    connections.async_acquire([&](auto response) { acquired.set_value(std::move(response)); });
    REQUIRE(initial.wait_for(5s) == std::future_status::ready);
    auto leased = initial.get();
    REQUIRE(leased);
    leased.connection.async_execute(
        {"ECHO", "held"},
        [&, capture](auto response) {
            ++observed.callbacks;
            completion.set_value(std::move(response));
        },
        30s);
    capture.reset();
    REQUIRE(sent.wait_for(5s) == std::future_status::ready);
    const auto injections = scoped_allocation_failure::injections.load();
    connections.async_acquire(
        [&](auto response) {
            CHECK(response.error == client_errc::invalid_command);
            close_inline(connections, unsigned(fault));
            closed.set_value();
        },
        -1ms);
    REQUIRE(close_done.wait_for(5s) == std::future_status::ready);
    close_done.get();
    CHECK(connections.close_and_wait());
    CHECK_FALSE(leased.connection);
    check_retirement(fault, injections);
    if (ready.wait_for(0ms) == std::future_status::ready)
    {
        const auto response = ready.get();
        CHECK(response.error == client_errc::closed);
        CHECK(response.command_may_have_run);
    }
    else
    {
        FAIL_CHECK("Accepted command callback was not terminalized before permanent close returned");
    }
    endpoint.finish();
}

class paired_server
{
    asio::io_context context_;
    asio::ip::tcp::acceptor acceptor_{context_, {asio::ip::address_v4::loopback(), 0}};
    std::promise<void> received_;
    std::atomic<unsigned> commands_{};
    std::promise<void> completion_;
    std::future<void> finished_ = completion_.get_future();
    std::thread worker_;

public:
    paired_server() {
        acceptor_.non_blocking(true);
        worker_ = std::thread([this] {
            std::vector<std::jthread> peers;
            std::mutex failures_mutex;
            std::exception_ptr failure;
            try
            {
                for (unsigned index{}; index != 2; ++index)
                {
                    asio::ip::tcp::socket socket(context_);
                    const auto deadline = std::chrono::steady_clock::now() + 5s;
                    for (;;)
                    {
                        arknet::error_code error;
                        acceptor_.accept(socket, error);
                        if (!error)
                        {
                            break;
                        }
                        if ((error != asio::error::would_block && error != asio::error::try_again) ||
                            std::chrono::steady_clock::now() >= deadline)
                        {
                            throw std::runtime_error("Two-child Redis peer accept failed");
                        }
                        std::this_thread::sleep_for(1ms);
                    }
                    peers.emplace_back([&, socket = std::move(socket)]() mutable {
                        try
                        {
                            peer remote(socket);
                            remote.hello();
                            remote.expect({"ECHO", "held"});
                            if (++commands_ == 2)
                            {
                                received_.set_value();
                            }
                            remote.wait_closed();
                        }
                        catch (...)
                        {
                            std::lock_guard lock(failures_mutex);
                            failure = std::current_exception();
                        }
                    });
                }
            }
            catch (...)
            {
                std::lock_guard lock(failures_mutex);
                failure = std::current_exception();
            }
            peers.clear();
            if (failure)
            {
                completion_.set_exception(failure);
            }
            else
            {
                completion_.set_value();
            }
        });
    }

    ~paired_server() {
        worker_.join();
    }

    std::string port() const {
        return std::to_string(acceptor_.local_endpoint().port());
    }

    auto received() {
        return received_.get_future();
    }

    void finish() {
        await(finished_);
    }
};

void close_two_children(unsigned fault) {
    reset_observations();
    paired_server endpoint;
    auto sent = endpoint.received();
    arknet::redis::pool_options options;
    options.max_connections = 2;
    options.connection.port = endpoint.port();
    observed_pool connections(options);
    std::array<std::promise<observed_pool::acquire_result>, 2> acquired;
    std::array<std::future<observed_pool::acquire_result>, 2> initial{
        acquired[0].get_future(), acquired[1].get_future()};
    std::array<std::promise<result>, 2> completion;
    std::array<std::future<result>, 2> ready{completion[0].get_future(), completion[1].get_future()};
    std::promise<void> closed;
    auto close_done = closed.get_future();
    auto retirement = retirement_guard(connections);
    std::array<observed_pool::acquire_result, 2> leased;
    for (std::size_t index{}; index != initial.size(); ++index)
    {
        connections.async_acquire([&, index](auto response) { acquired[index].set_value(std::move(response)); });
        REQUIRE(initial[index].wait_for(5s) == std::future_status::ready);
        leased[index] = initial[index].get();
        REQUIRE(leased[index]);
    }
    for (std::size_t index{}; index != ready.size(); ++index)
    {
        auto capture = std::make_shared<capture_probe>();
        leased[index].connection.async_execute(
            {"ECHO", "held"},
            [&, index, capture](auto response) {
                ++observed.callbacks;
                connections.close();
                CHECK_FALSE(connections.close_and_wait());
                CHECK(arknet::get_last_error() == asio::error::operation_not_supported);
                completion[index].set_value(std::move(response));
            },
            30s);
    }
    REQUIRE(sent.wait_for(5s) == std::future_status::ready);
    const auto injections = scoped_allocation_failure::injections.load();
    connections.async_acquire(
        [&](auto response) {
            CHECK(response.error == client_errc::invalid_command);
            close_inline(connections, fault);
            closed.set_value();
        },
        -1ms);
    REQUIRE(close_done.wait_for(5s) == std::future_status::ready);
    close_done.get();
    close_concurrently(connections);
    CHECK(connections.close_and_wait());
    check_retirement(true, injections, 2);
    for (std::size_t index{}; index != ready.size(); ++index)
    {
        CHECK_FALSE(leased[index].connection);
        REQUIRE(ready[index].wait_for(0ms) == std::future_status::ready);
        const auto response = ready[index].get();
        CHECK(response.error == client_errc::closed);
        CHECK(response.command_may_have_run);
    }
    endpoint.finish();
}

void actor_barrier(observed_pool& connections) {
    auto completed = std::make_shared<std::promise<client_errc>>();
    auto ready = completed->get_future();
    connections.async_acquire([completed](auto response) { completed->set_value(response.error); }, -1ms);
    CHECK(await(ready) == client_errc::invalid_command);
}

template<class Submit>
void fail_native_stop_after_actor(observed_pool& connections, Submit submit) {
    auto entered = std::make_shared<std::promise<void>>();
    auto ready = entered->get_future();
    auto published = std::make_shared<std::latch>(1);
    connections.async_acquire(
        [entered, published](auto response) {
            CHECK(response.error == client_errc::invalid_command);
            entered->set_value();
            published->wait();
            inject_on_stop = 1;
        },
        -1ms);
    if (ready.wait_for(5s) != std::future_status::ready)
    {
        published->count_down();
        FAIL("Pool actor did not reach native-stop arm barrier");
    }
    try
    { submit(); }
    catch (...)
    {
        published->count_down();
        throw;
    }
    published->count_down();
}

result ping(observed_pool::lease& leased) {
    auto completed = std::make_shared<std::promise<result>>();
    auto ready = completed->get_future();
    leased.async_execute({"PING"}, [completed](auto response) { completed->set_value(std::move(response)); });
    return await(ready);
}

void retained_retirement(bool failed_return) {
    reset_observations();
    unsigned accepted{};
    server endpoint(
        [&](peer& remote) {
            ++accepted;
            remote.hello();
            remote.expect({"PING"});
            remote.send(accepted == 1 ? "+FIRST\r\n" : "+SECOND\r\n");
            remote.wait_closed();
        },
        2);
    arknet::redis::pool_options options;
    options.max_connections = 1;
    options.connection.port = endpoint.port();
    observed_pool connections(options);
    auto acquired = std::make_shared<std::promise<observed_pool::acquire_result>>();
    auto initial = acquired->get_future();
    auto waited = std::make_shared<std::promise<observed_pool::acquire_result>>();
    auto ready = waited->get_future();
    auto retirement = retirement_guard(connections);
    connections.async_acquire([acquired](auto response) { acquired->set_value(std::move(response)); });
    auto held = std::make_unique<observed_pool::acquire_result>(await(initial));
    REQUIRE(*held);
    const auto reply = ping(held->connection);
    REQUIRE(reply);
    REQUIRE(reply.replies.size() == 1);
    CHECK(reply.replies.front().text == "FIRST");
    connections.async_acquire([waited](auto response) { waited->set_value(std::move(response)); });
    actor_barrier(connections);
    CHECK(ready.wait_for(0ms) != std::future_status::ready);
    const auto injections = scoped_allocation_failure::injections.load();
    if (failed_return)
    {
        {
            scoped_allocation_failure::scope failure;
            held.reset();
        }
        CHECK(scoped_allocation_failure::injections == injections + 1);
        fail_native_stop_after_actor(connections, [&] { connections.cancel(0); });
    }
    else
    {
        fail_native_stop_after_actor(connections, [&] {
            held->connection.discard();
            held.reset();
        });
    }
    // Invalid-options completion is only a lane fence, not a recovery pass.
    // The failed old child must still occupy its one bounded connection slot.
    actor_barrier(connections);
    const auto failures = unsigned(failed_return) + 1;
    CHECK(scoped_allocation_failure::injections == injections + failures);
    const auto diagnostic = connections.diagnostics();
    CHECK(diagnostic.return_submission_failures == failures);
    REQUIRE(diagnostic.last_return_submission_exception);
    CHECK_THROWS_AS(std::rethrow_exception(diagnostic.last_return_submission_exception), std::bad_alloc);
    CHECK(ready.wait_for(0ms) != std::future_status::ready);
    CHECK(observed.constructed == 1);
    connections.cancel(0);
    auto fresh = await(ready);
    REQUIRE(fresh);
    const auto second = ping(fresh.connection);
    REQUIRE(second);
    REQUIRE(second.replies.size() == 1);
    CHECK(second.replies.front().text == "SECOND");
    CHECK(reply.replies.front().text == "FIRST");
    CHECK(connections.close_and_wait());
    CHECK_FALSE(fresh.connection);
    CHECK(observed.constructed == 2);
    CHECK(observed.transports == 2);
    endpoint.finish();
    CHECK(accepted == 2);
}

} // namespace

DOCTEST_TEST_CASE("Redis inline pending connect close control") {
    pending_connect(false);
}

DOCTEST_TEST_CASE("Redis inline pending connect close allocation failure") {
    pending_connect(true);
}

DOCTEST_TEST_CASE("Redis pool child pending command close control") {
    pending_command(false);
}

DOCTEST_TEST_CASE("Redis pool child pending command close allocation failure") {
    pending_command(true);
}

DOCTEST_TEST_CASE("Redis pool first child native stop failure does not skip the second child") {
    close_two_children(1);
}

DOCTEST_TEST_CASE("Redis pool second child native stop failure retains both children through finalization") {
    close_two_children(2);
}

DOCTEST_TEST_CASE("Redis late connect retires a native stop that failed inside the connect observer") {
    reset_observations();
    server endpoint([](peer& remote) { remote.wait_closed(); });
    observed_client connection;
    std::promise<result> completion;
    auto ready = completion.get_future();
    auto capture = std::make_shared<capture_probe>();
    auto retirement = retirement_guard(connection);
    {
        std::lock_guard lock(observed_transport::callbacks_mutex);
        observed_transport::before_connect = [&] { close_inline(connection, 1); };
    }
    const auto injections = scoped_allocation_failure::injections.load();
    arknet::redis::connect_options target;
    target.port = endpoint.port();
    connection.async_connect(target, [&, capture](auto response) {
        ++observed.callbacks;
        completion.set_value(std::move(response));
    });
    capture.reset();
    REQUIRE(ready.wait_for(5s) == std::future_status::ready);
    const auto response = ready.get();
    CHECK(response.error == client_errc::closed);
    CHECK_FALSE(response.command_may_have_run);
    CHECK(connection.close_and_wait());
    check_retirement(true, injections);
    CHECK(observed.stop_entries >= 2);
    endpoint.finish();
}

DOCTEST_TEST_CASE("Redis discard retains its bounded slot after native stop failure") {
    retained_retirement(false);
}

DOCTEST_TEST_CASE("Redis failed return retains its bounded slot after native stop failure") {
    retained_retirement(true);
}
