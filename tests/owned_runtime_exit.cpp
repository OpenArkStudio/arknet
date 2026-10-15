// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string_view>

#if defined(ARKNET_TEST_MULTI_TU)
void arknet_test_hold_external_runtime();
#endif

namespace {

constinit std::atomic<unsigned> expected_callbacks{};
constinit std::atomic<unsigned> finished_callbacks{};
constinit std::atomic<unsigned> destroyed_resources{};
constinit std::atomic<bool> check_late_construction{};
constinit std::atomic<unsigned> late_constructions_rejected{};
constinit std::atomic<bool> multi_tu_exit{};

void fail(const char* message, int status) {
    std::fputs(message, stderr);
    std::fputc('\n', stderr);
    std::_Exit(status);
}

void verify_exit() {
    // Other translation units may initialize cleanup before this sentinel.
    // Their global-owner test is validated by ordinary, bounded process exit.
    if (multi_tu_exit.load())
    {
        return;
    }
    const auto expected = expected_callbacks.load();
    if (finished_callbacks.load() != expected || destroyed_resources.load() != expected)
    {
        fail("owned runtime exited before callbacks and resources were released", 41);
    }
    if (check_late_construction.load() && late_constructions_rejected.load() != 2)
    {
        fail("owned runtime accepted construction after process cleanup", 42);
    }
}

void try_late_construction();

// These callbacks are registered before the global pool is constructed. They
// therefore run after both the pool destructor and the runtime cleanup.
struct exit_checks
{
    exit_checks() {
        if (std::atexit(verify_exit) || std::atexit(try_late_construction))
        {
            fail("could not register process-exit checks", 43);
        }
    }
} checks;

} // namespace

// The sentinel must precede the header's ordered startup initialization.
#include <arknet/base/io_pool.hpp>

namespace {

using arknet::detail::io_pool;
using namespace std::chrono_literals;

class owned_endpoint : public arknet::detail::iopool_cp<owned_endpoint>
{
public:
    owned_endpoint()
        : iopool_cp(1) {}
};

void try_late_construction() {
    if (!check_late_construction.load())
    {
        return;
    }
    try
    { io_pool late(1); }
    catch (const std::logic_error&)
    { ++late_constructions_rejected; }
    try
    { owned_endpoint late; }
    catch (const std::logic_error&)
    { ++late_constructions_rejected; }
}

// Its destructor registration predates direct global_pool construction. This
// owner only acquires a pool in main, independently of its own initialization.
std::shared_ptr<io_pool> indirect_owner;
io_pool global_pool(1);

struct callback_resource
{
#if defined(ARKNET_ENABLE_SSL)
    // Constructed in main, after runtime cleanup has already been registered.
    std::unique_ptr<asio::ssl::context> tls = std::make_unique<asio::ssl::context>(asio::ssl::context::tls_client);
#endif

    ~callback_resource() {
#if defined(ARKNET_ENABLE_SSL)
        tls.reset();
#endif
        ++destroyed_resources;
    }
};

using owned_pool = std::shared_ptr<io_pool>;

owned_pool make_pool() {
    owned_pool pool(new io_pool(1), arknet::detail::iopool_deleter{});
    if (!pool->start())
    {
        fail("could not start owned IO pool", 44);
    }
    return pool;
}

void schedule_owned(owned_pool pool, bool nested, std::chrono::milliseconds delay) {
    auto timer = std::make_shared<asio::steady_timer>(pool->get_context(), delay);
    auto resource = std::make_shared<callback_resource>();
    ++expected_callbacks;
    timer->async_wait([pool = std::move(pool), timer, resource = std::move(resource), nested](auto error) mutable {
        if (error)
        {
            fail("owned callback was unexpectedly cancelled", 45);
        }
        if (nested)
        {
            schedule_owned(make_pool(), false, 10ms);
        }
        ++finished_callbacks;
        // Last ownership is released on the worker. Its TLS resource remains
        // captured until this handler returns and the cleanup worker joins it.
        pool.reset();
    });
}

#if defined(ARKNET_ENABLE_SSL)
void verify_openssl_exit() {
    if (destroyed_resources.load() != expected_callbacks.load())
    {
        fail("OpenSSL cleanup started before owned TLS resources were destroyed", 46);
    }
}
#endif

} // namespace

int main(int argc, char** argv) {
    const std::string_view mode = argc > 1 ? argv[1] : "callback";
    // Baseline implementations initialize their cleanup from endpoint wrappers.
    // Explicit initialization keeps this direct-pool test equivalent.
    arknet::detail::pool_cleanup();
#if defined(ARKNET_ENABLE_SSL)
    if (!OPENSSL_init_ssl(0, nullptr) || !OPENSSL_atexit(verify_openssl_exit))
    {
        fail("could not initialize OpenSSL exit check", 47);
    }
#endif
    if (mode == "global")
    {
        if (!global_pool.start())
        {
            return 48;
        }
        auto timer = std::make_shared<asio::steady_timer>(global_pool.get_context(), 1s);
        auto resource = std::make_shared<callback_resource>();
        ++expected_callbacks;
        timer->async_wait([timer, resource = std::move(resource)](auto error) {
            if (error)
            {
                fail("global pool callback was unexpectedly cancelled", 49);
            }
            ++finished_callbacks;
        });
    }
    else if (mode == "callback" || mode == "nested")
    {
        schedule_owned(make_pool(), mode == "nested", 1s);
    }
    else if (mode == "late")
    {
        check_late_construction = true;
    }
    else if (mode == "indirect")
    {
        indirect_owner = make_pool();
    }
#if defined(ARKNET_TEST_MULTI_TU)
    else if (mode == "external")
    {
        multi_tu_exit = true;
        indirect_owner = make_pool();
        arknet_test_hold_external_runtime();
    }
#endif
    else
    {
        return 50;
    }
    // Returning while the timer is pending deliberately overlaps ordinary
    // process teardown with a callback that owns its last pool reference.
}
