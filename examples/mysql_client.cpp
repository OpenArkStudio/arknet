// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <arknet/mysql/connection.hpp>

namespace {

template<class Result, class Submit>
Result wait_for(arknet::mysql::connection& client, Submit submit) {
    auto completed = std::make_shared<std::promise<Result>>();
    auto completion = completed->get_future();
    submit([completed](Result result) { completed->set_value(std::move(result)); });
    // Waiting is confined to main. The callback owns its promise if a deadline closes the connection.
    if (completion.wait_for(std::chrono::seconds(15)) != std::future_status::ready)
    {
        client.close();
        throw std::runtime_error("MySQL example operation deadline exceeded");
    }
    return completion.get();
}

void require(const arknet::mysql::connection_result& status, std::string_view operation) {
    if (!status)
    {
        throw std::runtime_error(std::string(operation) +
                                 " failed: client_code=" + std::to_string(static_cast<int>(status.code)) +
                                 ", server_code=" + std::to_string(status.server_code) +
                                 ", outcome_unknown=" + (status.outcome_unknown ? "true" : "false"));
    }
}

std::uint16_t port_number(std::string_view text) {
    unsigned value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0 || value > 65535)
    {
        throw std::invalid_argument("PORT must be between 1 and 65535");
    }
    return static_cast<std::uint16_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    try
    {
        if (argc != 3)
        {
            std::cerr << "Usage: arknet_mysql_client HOST PORT\n"
                         "Environment: ARKNET_MYSQL_USERNAME, ARKNET_MYSQL_PASSWORD, optional "
                         "ARKNET_MYSQL_DATABASE, ARKNET_MYSQL_CA_FILE, ARKNET_MYSQL_SERVER_NAME\n";
            return 1;
        }
        arknet::mysql::connect_options options;
        options.host = argv[1];
        options.port = port_number(argv[2]);
        const auto* username = std::getenv("ARKNET_MYSQL_USERNAME");
        if (!username || !*username)
        {
            throw std::invalid_argument("Set ARKNET_MYSQL_USERNAME");
        }
        options.username = username;
        if (const auto* password = std::getenv("ARKNET_MYSQL_PASSWORD"))
        {
            options.password = password;
        }
        if (const auto* database = std::getenv("ARKNET_MYSQL_DATABASE"))
        {
            options.database = database;
        }
        if (const auto* authority = std::getenv("ARKNET_MYSQL_CA_FILE"))
        {
            options.ca_file = authority;
        }
        if (const auto* identity = std::getenv("ARKNET_MYSQL_SERVER_NAME"))
        {
            options.server_name = identity;
        }

        arknet::mysql::connection client;
        require(wait_for<arknet::mysql::connection_result>(
                    client, [&](auto completed) { client.async_connect(options, std::move(completed)); }),
            "Connect");
        auto query = wait_for<arknet::mysql::query_result>(client, [&](auto completed) {
            client.async_query("SELECT 42 AS number, 'arknet' AS name, NULL AS absent", std::move(completed));
        });
        require(query.status, "Query");
        if (query.rows.size() != 1 || query.rows.front().size() != 3 ||
            std::get<std::int64_t>(query.rows[0][0]) != 42 || std::get<std::string>(query.rows[0][1]) != "arknet" ||
            !std::holds_alternative<std::monostate>(query.rows[0][2]))
        {
            throw std::runtime_error("Unexpected query result");
        }
        auto prepared = wait_for<arknet::mysql::prepare_result>(client,
            [&](auto completed) { client.async_prepare("SELECT ? AS number, ? AS payload", std::move(completed)); });
        require(prepared.status, "Prepare");
        if (prepared.statement.parameter_count() != 2)
        {
            throw std::runtime_error("Unexpected prepared parameter count");
        }
        auto began = wait_for<arknet::mysql::query_result>(
            client, [&](auto completed) { client.async_begin(std::move(completed)); });
        require(began.status, "Begin transaction");
        const std::string payload("ark\0net", 7);
        auto executed = wait_for<arknet::mysql::query_result>(client, [&](auto completed) {
            client.async_execute(prepared.statement, {std::int64_t(42), payload}, std::move(completed));
        });
        require(executed.status, "Execute");
        if (executed.rows.size() != 1 || executed.rows.front().size() != 2 ||
            std::get<std::int64_t>(executed.rows[0][0]) != 42 || std::get<std::string>(executed.rows[0][1]) != payload)
        {
            throw std::runtime_error("Unexpected prepared result");
        }
        auto committed = wait_for<arknet::mysql::query_result>(
            client, [&](auto completed) { client.async_commit(std::move(completed)); });
        require(committed.status, "Commit transaction");
        require(wait_for<arknet::mysql::connection_result>(
                    client, [&](auto completed) { client.async_close(prepared.statement, std::move(completed)); }),
            "Close statement");
        client.close();
        std::cout << "Query, typed prepared parameters and transaction verified over TLS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MySQL example: " << error.what() << '\n';
        return 1;
    }
}
