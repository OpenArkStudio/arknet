#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "vendor/doctest/doctest.h"

#include <arknet/mysql/connection.hpp>

#ifndef ARKNET_TEST_CERT_DIR
#error "ARKNET_TEST_CERT_DIR must name the test certificate directory"
#endif

using namespace std::chrono_literals;

namespace mysql_test {

using arknet::error_code;
using arknet::mysql::connect_options;
using arknet::mysql::connection;
using arknet::mysql::connection_errc;
using arknet::mysql::connection_result;
using arknet::mysql::tls_mode;
namespace wire = arknet::mysql::detail;

class mysql_peer
{
    asio::io_context context_;
    asio::ip::tcp::acceptor acceptor_{context_, {asio::ip::address_v4::loopback(), 0}};
    asio::ssl::context tls_context_{asio::ssl::context::tls_server};
    asio::ssl::stream<asio::ip::tcp::socket> stream_{context_, tls_context_};
    asio::steady_timer deadline_{context_};
    std::promise<void> completion_;
    std::future<void> result_ = completion_.get_future();
    std::thread worker_;
    bool encrypted_ = false;

public:
    std::uint8_t sequence = 0;
    std::string observed_sni;

    explicit mysql_peer(std::function<asio::awaitable<void>(mysql_peer&)> program, bool wrong_identity = false) {
        const std::string directory = ARKNET_TEST_CERT_DIR;
        tls_context_.use_certificate_chain_file(directory + (wrong_identity ? "/wrong.pem" : "/server.pem"));
        tls_context_.use_private_key_file(directory + "/server-key.pem", asio::ssl::context::pem);
        // SSL_new was called before the identity was loaded into SSL_CTX.
        REQUIRE(SSL_use_certificate_file(stream_.native_handle(),
                    (directory + (wrong_identity ? "/wrong.pem" : "/server.pem")).c_str(), SSL_FILETYPE_PEM) == 1);
        REQUIRE(SSL_use_PrivateKey_file(
                    stream_.native_handle(), (directory + "/server-key.pem").c_str(), SSL_FILETYPE_PEM) == 1);
        deadline_.expires_after(5s);
        deadline_.async_wait([this](error_code error) {
            if (!error)
            {
                error_code ignored;
                acceptor_.close(ignored);
                stream_.next_layer().close(ignored);
            }
        });
        asio::co_spawn(
            context_,
            [this, program = std::move(program)]() -> asio::awaitable<void> {
                co_await acceptor_.async_accept(stream_.next_layer(), asio::use_awaitable);
                co_await program(*this);
            },
            [this](std::exception_ptr exception) {
                deadline_.cancel();
                if (exception)
                {
                    completion_.set_exception(exception);
                }
                else
                {
                    completion_.set_value();
                }
            });
        worker_ = std::thread([this] { context_.run(); });
    }

    ~mysql_peer() {
        context_.stop();
        worker_.join();
    }

    std::uint16_t port() const {
        return acceptor_.local_endpoint().port();
    }

    asio::awaitable<void> write_raw(std::string bytes, bool fragment = false) {
        const auto stride = fragment ? 1u : bytes.size();
        for (std::size_t offset = 0; offset < bytes.size(); offset += stride)
        {
            const auto buffer = asio::buffer(bytes.data() + offset, (std::min)(stride, bytes.size() - offset));
            if (encrypted_)
            {
                co_await asio::async_write(stream_, buffer, asio::use_awaitable);
            }
            else
            {
                co_await asio::async_write(stream_.next_layer(), buffer, asio::use_awaitable);
            }
        }
    }

    asio::awaitable<void> send(std::string payload, bool fragment = false) {
        std::string bytes;
        REQUIRE(wire::encode_message(payload, sequence, bytes));
        co_await write_raw(std::move(bytes), fragment);
    }

    asio::awaitable<std::string> receive(std::size_t maximum_size = 4096) {
        std::array<unsigned char, 4> header{};
        if (encrypted_)
        {
            co_await asio::async_read(stream_, asio::buffer(header), asio::use_awaitable);
        }
        else
        {
            co_await asio::async_read(stream_.next_layer(), asio::buffer(header), asio::use_awaitable);
        }
        REQUIRE(header[3] == sequence++);
        const std::size_t size = header[0] | (std::size_t(header[1]) << 8) | (std::size_t(header[2]) << 16);
        REQUIRE(size <= maximum_size);
        std::string payload(size, '\0');
        if (encrypted_)
        {
            co_await asio::async_read(stream_, asio::buffer(payload), asio::use_awaitable);
        }
        else
        {
            co_await asio::async_read(stream_.next_layer(), asio::buffer(payload), asio::use_awaitable);
        }
        co_return payload;
    }

    asio::awaitable<void> upgrade() {
        co_await stream_.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);
        encrypted_ = true;
        const char* name = SSL_get_servername(stream_.native_handle(), TLSEXT_NAMETYPE_host_name);
        observed_sni = name ? name : "";
    }

    asio::awaitable<void> wait_for_eof() {
        std::array<char, 4096> bytes{};
        error_code error;
        do
        {
            if (encrypted_)
            {
                const auto size = co_await stream_.async_read_some(
                    asio::buffer(bytes), asio::redirect_error(asio::use_awaitable, error));
                CHECK(size == 0);
            }
            else
            {
                co_await stream_.next_layer().async_read_some(
                    asio::buffer(bytes), asio::redirect_error(asio::use_awaitable, error));
            }
        } while (!error);
        CHECK((error == asio::error::eof || error == asio::error::connection_reset ||
               error == asio::ssl::error::stream_truncated));
    }

    void disconnect() {
        error_code ignored;
        stream_.next_layer().shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
        stream_.next_layer().close(ignored);
    }

    void finish() {
        REQUIRE(result_.wait_for(6s) == std::future_status::ready);
        result_.get();
    }
};

inline constexpr auto basic_caps = wire::capability::protocol_41 | wire::capability::secure_connection |
                                   wire::capability::plugin_auth | wire::capability::transactions |
                                   wire::capability::connect_with_db;
inline const std::string challenge = "12345678901234567890";
inline const std::string ok_packet("\0\0\0\2\0\0\0", 7);

inline std::string greeting(std::string_view plugin = "mysql_native_password", bool tls = false) {
    const auto capabilities = basic_caps | (tls ? wire::capability::ssl : 0);
    std::string payload(1, '\x0a');
    payload.append("8.4.11-test");
    payload.push_back('\0');
    wire::append_integer(payload, 1234, 4);
    payload.append(challenge, 0, 8);
    payload.push_back('\0');
    wire::append_integer(payload, capabilities & 0xffff, 2);
    payload.push_back(45);
    wire::append_integer(payload, 2, 2);
    wire::append_integer(payload, capabilities >> 16, 2);
    payload.push_back(21);
    payload.append(10, '\0');
    payload.append(challenge, 8, 12);
    payload.push_back('\0');
    payload.append(plugin);
    payload.push_back('\0');
    return payload;
}

inline connect_options options_for(const mysql_peer& peer, bool tls = false) {
    connect_options options;
    options.host = "127.0.0.1";
    options.port = peer.port();
    options.username = "test-user";
    options.password = "secret";
    options.database = "test-db";
    options.tls = tls ? tls_mode::required : tls_mode::disabled;
    options.ca_file = std::string(ARKNET_TEST_CERT_DIR) + "/ca.pem";
    options.server_name = "localhost";
    options.timeout = 2s;
    return options;
}

inline void check_response(std::string_view payload, std::string_view plugin, bool tls) {
    wire::wire_reader reader(payload);
    std::uint64_t caps = 0;
    REQUIRE(reader.read_integer(4, caps) == wire::read_status::complete);
    CHECK(bool(caps & wire::capability::ssl) == tls);
    CHECK(bool(caps & wire::capability::connect_with_db));
    std::string_view ignored;
    REQUIRE(reader.read_bytes(28, ignored) == wire::read_status::complete);
    std::string_view username;
    REQUIRE(reader.read_null_terminated(username) == wire::read_status::complete);
    CHECK(username == "test-user");
    std::uint64_t auth_size = 0;
    REQUIRE(reader.read_integer(1, auth_size) == wire::read_status::complete);
    std::string_view response;
    REQUIRE(reader.read_bytes(auth_size, response) == wire::read_status::complete);
    std::string expected;
    REQUIRE(wire::make_auth_response(plugin, "secret", challenge, expected) == wire::auth_status::complete);
    CHECK(response == expected);
    std::string_view database;
    REQUIRE(reader.read_null_terminated(database) == wire::read_status::complete);
    CHECK(database == "test-db");
    std::string_view mechanism;
    REQUIRE(reader.read_null_terminated(mechanism) == wire::read_status::complete);
    CHECK(mechanism == plugin);
    CHECK(reader.remaining() == 0);
}

inline connection_result connect(connection& client, connect_options options) {
    std::promise<connection_result> completion;
    auto result = completion.get_future();
    client.async_connect(
        std::move(options), [&completion](connection_result value) { completion.set_value(std::move(value)); });
    REQUIRE(result.wait_for(5s) == std::future_status::ready);
    return result.get();
}

} // namespace mysql_test

namespace mysql_test {

using arknet::mysql::field_value;
using arknet::mysql::prepare_result;
using arknet::mysql::prepared_statement;
using arknet::mysql::query_options;
using arknet::mysql::query_result;

inline query_result query(connection& client, std::string sql, query_options options = {}) {
    std::promise<query_result> completion;
    auto future = completion.get_future();
    client.async_query(
        std::move(sql), [&completion](query_result result) { completion.set_value(std::move(result)); }, options);
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

inline prepare_result prepare(connection& client, std::string sql, query_options options = {}) {
    std::promise<prepare_result> completion;
    auto future = completion.get_future();
    client.async_prepare(
        std::move(sql), [&completion](prepare_result result) { completion.set_value(std::move(result)); }, options);
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

inline query_result execute(connection& client, const prepared_statement& statement,
    std::vector<field_value> parameters, query_options options = {}) {
    std::promise<query_result> completion;
    auto future = completion.get_future();
    client.async_execute(
        statement, std::move(parameters),
        [&completion](query_result result) { completion.set_value(std::move(result)); }, options);
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

inline connection_result close_statement(connection& client, const prepared_statement& statement) {
    std::promise<connection_result> completion;
    auto future = completion.get_future();
    client.async_close(statement, [&completion](connection_result result) { completion.set_value(std::move(result)); });
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

inline connection_result reset(connection& client) {
    std::promise<connection_result> completion;
    auto future = completion.get_future();
    client.async_reset([&completion](connection_result result) { completion.set_value(std::move(result)); });
    REQUIRE(future.wait_for(6s) == std::future_status::ready);
    return future.get();
}

inline asio::awaitable<void> authenticate(mysql_peer& server) {
    co_await server.send(greeting());
    co_await server.receive();
    co_await server.send(ok_packet);
}

inline asio::awaitable<std::string> receive_command(mysql_peer& server) {
    server.sequence = 0;
    co_return co_await server.receive();
}

inline std::string column_packet(std::string_view name, wire::field_type type = wire::field_type::longlong) {
    std::string payload;
    for (const auto value :
        {std::string_view("def"), std::string_view("test-db"), std::string_view(""), std::string_view(""), name, name})
    {
        wire::append_string(payload, value);
    }
    payload.push_back('\x0c');
    wire::append_integer(payload, 63, 2);
    wire::append_integer(payload, 20, 4);
    payload.push_back(static_cast<char>(type));
    wire::append_integer(payload, 0, 2);
    payload.append(3, '\0');
    return payload;
}

inline const std::string eof_packet("\xfe\0\0\2\0", 5);

inline asio::awaitable<void> one_integer(mysql_peer& server, std::string value, bool fragment = false) {
    co_await server.send(std::string(1, '\1'), fragment);
    co_await server.send(column_packet("value"), fragment);
    co_await server.send(eof_packet, fragment);
    std::string row;
    wire::append_string(row, value);
    co_await server.send(row, fragment);
    co_await server.send(eof_packet, fragment);
}

inline connect_options real_options() {
    const auto environment = [](const char* name) -> std::string {
        const char* value = std::getenv(name);
        REQUIRE_MESSAGE(value != nullptr, name, " must be set for real MySQL tests");
        return value;
    };
    connect_options options;
    options.host = environment("ARKNET_MYSQL_TEST_HOST");
    options.port = static_cast<std::uint16_t>(std::stoul(environment("ARKNET_MYSQL_TEST_PORT")));
    options.username = environment("ARKNET_MYSQL_TEST_USER");
    options.password = environment("ARKNET_MYSQL_TEST_PASSWORD");
    options.database = environment("ARKNET_MYSQL_TEST_DATABASE");
    options.ca_file = environment("ARKNET_MYSQL_TEST_CA");
    options.timeout = 5s;
    return options;
}

} // namespace mysql_test
