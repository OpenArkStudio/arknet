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

#include <arknet/mysql/pool.hpp>

namespace {

template<class Result, class Submit>
Result wait_for(arknet::mysql::pool& pool, Submit submit) {
    auto completed = std::make_shared<std::promise<Result>>();
    auto completion = completed->get_future();
    submit([completed](Result response) { completed->set_value(std::move(response)); });
    if (completion.wait_for(std::chrono::seconds(15)) != std::future_status::ready)
    {
        pool.close();
        throw std::runtime_error("MySQL pool example operation deadline exceeded");
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
            std::cerr << "Usage: arknet_mysql_pool HOST PORT\n"
                         "Environment: ARKNET_MYSQL_USERNAME, ARKNET_MYSQL_PASSWORD, optional "
                         "ARKNET_MYSQL_DATABASE, ARKNET_MYSQL_CA_FILE, ARKNET_MYSQL_SERVER_NAME\n";
            return 1;
        }
        arknet::mysql::pool_options options;
        options.max_connections = 2;
        options.max_waiters = 64;
        options.io_threads = 1;
        options.connection.host = argv[1];
        options.connection.port = port_number(argv[2]);
        const auto* username = std::getenv("ARKNET_MYSQL_USERNAME");
        if (!username || !*username)
        {
            throw std::invalid_argument("Set ARKNET_MYSQL_USERNAME");
        }
        options.connection.username = username;
        if (const auto* password = std::getenv("ARKNET_MYSQL_PASSWORD"))
        {
            options.connection.password = password;
        }
        if (const auto* database = std::getenv("ARKNET_MYSQL_DATABASE"))
        {
            options.connection.database = database;
        }
        if (const auto* authority = std::getenv("ARKNET_MYSQL_CA_FILE"))
        {
            options.connection.ca_file = authority;
        }
        if (const auto* identity = std::getenv("ARKNET_MYSQL_SERVER_NAME"))
        {
            options.connection.server_name = identity;
        }
        arknet::mysql::pool pool(options);
        auto acquired = wait_for<arknet::mysql::pool::acquire_result>(
            pool, [&](auto completed) { pool.async_acquire(std::move(completed)); });
        require(acquired.status, "Acquire connection");
        if (!acquired)
        {
            throw std::runtime_error("MySQL pool lease is unavailable");
        }
        auto connection = std::move(acquired.connection);
        auto began = wait_for<arknet::mysql::query_result>(
            pool, [&](auto completed) { connection.async_begin(std::move(completed)); });
        require(began.status, "Begin transaction");
        auto query = wait_for<arknet::mysql::query_result>(
            pool, [&](auto completed) { connection.async_query("SELECT 42 AS number", std::move(completed)); });
        require(query.status, "Query");
        if (query.rows.size() != 1 || query.rows.front().size() != 1 ||
            std::get<std::int64_t>(query.rows.front().front()) != 42)
        {
            throw std::runtime_error("Unexpected MySQL pooled query result");
        }
        auto committed = wait_for<arknet::mysql::query_result>(
            pool, [&](auto completed) { connection.async_commit(std::move(completed)); });
        require(committed.status, "Commit transaction");
        // The same exclusive lease covered the complete transaction. Its destructor also calls release().
        connection.release();
        pool.close();
        std::cout << "Pooled query and exclusive transaction verified over TLS\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "MySQL pool example: " << error.what() << '\n';
        return 1;
    }
}
