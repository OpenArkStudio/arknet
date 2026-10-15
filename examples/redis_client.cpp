// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <arknet/redis/client.hpp>

namespace {

template<class Client>
int run(const arknet::redis::connect_options& target, const arknet::redis::client_options& options) {
    Client client(options);
    auto connected = std::make_shared<std::promise<arknet::redis::result>>();
    auto connection = connected->get_future();
    client.async_connect(
        target, [connected](arknet::redis::result response) { connected->set_value(std::move(response)); });
    if (connection.wait_for(std::chrono::seconds(10)) != std::future_status::ready || !connection.get())
    {
        client.close();
        std::cerr << "Redis connection or authentication failed\n";
        return 1;
    }

    const std::string payload("ark\0net", 7);
    auto completed = std::make_shared<std::promise<arknet::redis::result>>();
    auto completion = completed->get_future();
    client.async_pipeline({{"PING"}, {"ECHO", payload}},
        [completed](arknet::redis::result response) { completed->set_value(std::move(response)); });
    if (completion.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
    {
        client.close();
        std::cerr << "Redis request deadline exceeded\n";
        return 1;
    }
    auto response = completion.get();
    client.close();
    if (!response)
    {
        std::cerr << "Redis request failed; command_may_have_run=" << response.command_may_have_run << '\n';
        return 1;
    }
    if (response.replies.size() != 2 || response.replies[0].type != arknet::redis::value_type::simple_string ||
        response.replies[0].text != "PONG" || response.replies[1].type != arknet::redis::value_type::bulk_string ||
        response.replies[1].text != payload)
    {
        std::cerr << "Unexpected Redis reply\n";
        return 1;
    }
    std::cout << "PING: PONG; binary ECHO: " << response.replies[1].text.size() << " bytes verified\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try
    {
        if (argc < 3 || argc > 5 || (argc >= 4 && std::string_view(argv[3]) != "--tls"))
        {
            std::cerr << "Usage: arknet_redis_client HOST PORT [--tls [CA_FILE]]\n"
                         "Optional environment: ARKNET_REDIS_USERNAME, ARKNET_REDIS_PASSWORD\n";
            return 1;
        }
        arknet::redis::connect_options target;
        target.host = argv[1];
        target.port = argv[2];
        if (const auto* username = std::getenv("ARKNET_REDIS_USERNAME"))
        {
            target.username = username;
        }
        if (const auto* password = std::getenv("ARKNET_REDIS_PASSWORD"))
        {
            target.password = password;
        }
        arknet::redis::client_options options;
        if (argc == 5)
        {
            options.tls_certificate_authority = argv[4];
        }
        if (argc >= 4)
        {
#if defined(ARKNET_ENABLE_SSL)
            return run<arknet::redis::tls_client>(target, options);
#else
            throw std::invalid_argument("TLS requires ARKNET_ENABLE_SSL");
#endif
        }
        return run<arknet::redis::client>(target, options);
    }
    catch (const std::exception& error)
    {
        std::cerr << "Redis example: " << error.what() << '\n';
        return 1;
    }
}
