// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <arknet/redis/pool.hpp>

namespace {

template<class Pool>
int run(const arknet::redis::pool_options& options) {
    Pool pool(options);
    auto acquired = std::make_shared<std::promise<typename Pool::acquire_result>>();
    auto acquisition = acquired->get_future();
    pool.async_acquire(
        [acquired](typename Pool::acquire_result response) { acquired->set_value(std::move(response)); });
    if (acquisition.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
    {
        pool.close();
        std::cerr << "Redis pool acquisition deadline exceeded\n";
        return 1;
    }
    auto response = acquisition.get();
    if (!response)
    {
        pool.close();
        std::cerr << "Redis pool acquisition failed\n";
        return 1;
    }
    auto connection = std::move(response.connection);
    auto completed = std::make_shared<std::promise<arknet::redis::result>>();
    auto completion = completed->get_future();
    connection.async_execute(
        {"PING"}, [completed](arknet::redis::result reply) { completed->set_value(std::move(reply)); });
    if (completion.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
    {
        connection.discard();
        pool.close();
        std::cerr << "Redis pooled request deadline exceeded\n";
        return 1;
    }
    auto reply = completion.get();
    // release() is also called by the lease destructor. RESET completes before another borrower can use it.
    connection.release();
    pool.close();
    if (!reply || reply.replies.size() != 1 || reply.replies.front().type != arknet::redis::value_type::simple_string ||
        reply.replies.front().text != "PONG")
    {
        std::cerr << "Redis pooled PING failed; command_may_have_run=" << reply.command_may_have_run << '\n';
        return 1;
    }
    std::cout << "Pooled PING: PONG\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try
    {
        if (argc < 3 || argc > 5 || (argc >= 4 && std::string_view(argv[3]) != "--tls"))
        {
            std::cerr << "Usage: arknet_redis_pool HOST PORT [--tls [CA_FILE]]\n"
                         "Optional environment: ARKNET_REDIS_USERNAME, ARKNET_REDIS_PASSWORD\n";
            return 1;
        }
        arknet::redis::pool_options options;
        options.max_connections = 4;
        options.max_waiters = 64;
        options.connection.host = argv[1];
        options.connection.port = argv[2];
        if (const auto* username = std::getenv("ARKNET_REDIS_USERNAME"))
        {
            options.connection.username = username;
        }
        if (const auto* password = std::getenv("ARKNET_REDIS_PASSWORD"))
        {
            options.connection.password = password;
        }
        if (argc == 5)
        {
            options.certificate_authority = argv[4];
        }
        if (argc >= 4)
        {
#if defined(ARKNET_ENABLE_SSL)
            return run<arknet::redis::tls_pool>(options);
#else
            throw std::invalid_argument("TLS requires ARKNET_ENABLE_SSL");
#endif
        }
        return run<arknet::redis::pool>(options);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Redis pool example: " << error.what() << '\n';
        return 1;
    }
}
