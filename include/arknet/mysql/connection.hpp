// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arknet/base/detail/deadline.hpp>
#include <arknet/base/io_pool.hpp>
#include <arknet/mysql/detail/auth.hpp>
#include <arknet/mysql/detail/command.hpp>
#include <arknet/mysql/detail/handshake.hpp>

#if !defined(ARKNET_ENABLE_SSL)
#error "MySQL connections require ARKNET_ENABLE_SSL for authenticated TLS and password hashing"
#endif

namespace arknet::mysql {

enum class tls_mode
{
    required,
    disabled
};

enum class connection_errc
{
    none,
    invalid_options,
    already_started,
    cancelled,
    timeout,
    transport,
    protocol,
    tls_required,
    tls,
    unsupported_auth,
    authentication,
    not_connected,
    queue_full,
    result_limit,
    invalid_statement,
    unsupported,
    server
};

struct connection_result
{
    connection_errc code = connection_errc::none;
    error_code transport_error;
    std::uint16_t server_code = 0;
    std::string sql_state;
    std::string message;
    bool outcome_unknown = false;

    explicit operator bool() const noexcept {
        return code == connection_errc::none;
    }
};

struct connect_options
{
    std::string host = "localhost";
    std::uint16_t port = 3306;
    std::string username;
    std::string password;
    std::string database;
    tls_mode tls = tls_mode::required;
    std::string ca_file;
    // Defaults to host. An override changes certificate identity and DNS SNI,
    // never the network destination or the requirement to verify the peer.
    std::string server_name;
    std::chrono::milliseconds timeout{10000};
    std::size_t max_message_size = detail::default_message_limit;
    // Additional requests beyond the one active request. Admission happens
    // before posting onto the IO lane. Byte limits include retained request
    // and parameter storage for active, queued and not-yet-posted work.
    std::size_t max_pending_commands = 64;
    std::size_t max_pending_bytes = detail::default_message_limit;
};

using field_type = detail::field_type;
using field_value = detail::field_value;
using row_values = detail::row_values;
using column_definition = detail::column_definition;
using date_value = detail::date_value;
using datetime_value = detail::datetime_value;
using time_value = detail::time_value;
using request_id = std::uint64_t;

struct query_options
{
    std::chrono::milliseconds timeout{10000};
    std::size_t max_rows = 100000;
    std::size_t max_columns = 4096;
    std::size_t max_result_bytes = detail::default_message_limit;
};

struct query_result
{
    connection_result status;
    std::vector<column_definition> columns;
    std::vector<row_values> rows;
    std::uint64_t affected_rows = 0;
    std::uint64_t last_insert_id = 0;
    std::uint16_t status_flags = 0;
    std::uint16_t warnings = 0;

    explicit operator bool() const noexcept {
        return bool(status);
    }
};

class connection;
class pool;

class prepared_statement
{
    friend class connection;

    struct control
    {
        std::weak_ptr<void> owner;
        std::uint64_t generation = 0;
        std::uint64_t session_version = 0;
        std::uint32_t id = 0;
        std::size_t parameter_count = 0;
        std::vector<column_definition> columns;
        std::atomic<bool> closed{};
        std::function<void()> cleanup;

        ~control() {
            if (!closed.exchange(true) && cleanup)
            {
                cleanup();
            }
        }
    };

    std::shared_ptr<control> control_;

public:
    prepared_statement() = default;

    bool is_open() const noexcept {
        return control_ && !control_->closed.load() && !control_->owner.expired();
    }

    std::size_t parameter_count() const noexcept {
        return control_ ? control_->parameter_count : 0;
    }

    const std::vector<column_definition>& columns() const noexcept {
        static const std::vector<column_definition> empty;
        return control_ ? control_->columns : empty;
    }
};

struct prepare_result
{
    connection_result status;
    prepared_statement statement;

    explicit operator bool() const noexcept {
        return bool(status);
    }
};

// Completion handlers run exactly once on the library-owned IO lane and must
// not throw. close() cancels a pending connect and can be called in its handler.
// The connection itself may also be destroyed in a completion handler.
class connection
{
    friend class pool;

public:
    using connect_handler = std::function<void(connection_result)>;
    using query_handler = std::function<void(query_result)>;
    using prepare_handler = std::function<void(prepare_result)>;
    using result_handler = std::function<void(connection_result)>;

private:
    struct attempt
    {
        connect_options options;
        connect_handler completion;
        std::uint64_t generation;
        asio::ssl::context tls_context{asio::ssl::context::tls_client};
        asio::ssl::stream<asio::ip::tcp::socket> stream;
        asio::ip::tcp::resolver resolver;
        asio::steady_timer deadline;
        detail::packet_decoder decoder;
        detail::server_greeting greeting;
        std::array<char, 4096> read_buffer{};
        std::string incoming;
        std::string outgoing;
        std::string auth_plugin;
        std::uint32_t capabilities = 0;
        std::uint8_t sequence = 0;
        unsigned auth_rounds = 0;
        bool encrypted = false;
        bool completed = false;
        bool fast_auth_complete = false;
        std::uint64_t session_version = 0;

        attempt(asio::any_io_executor executor, connect_options settings, connect_handler handler, std::uint64_t epoch)
            : options(std::move(settings))
            , completion(std::move(handler))
            , generation(epoch)
            , stream(executor, tls_context)
            , resolver(executor)
            , deadline(executor)
            , decoder(options.max_message_size) {}

        ~attempt() {
            OPENSSL_cleanse(options.password.data(), options.password.size());
            OPENSSL_cleanse(outgoing.data(), outgoing.size());
        }

        void cancel() {
            resolver.cancel();
            error_code ignored;
            deadline.cancel();
            stream.next_layer().cancel(ignored);
            stream.next_layer().close(ignored);
        }
    };

    enum class command_kind
    {
        query,
        prepare,
        execute,
        close_statement,
        reset
    };
    enum class command_phase
    {
        first,
        columns,
        column_end,
        rows,
        prepare_parameters,
        prepare_parameter_end,
        prepare_columns,
        prepare_column_end
    };

    struct command
    {
        request_id id = 0;
        command_kind kind = command_kind::query;
        command_phase phase = command_phase::first;
        query_options options;
        std::uint64_t generation = 0;
        std::shared_ptr<attempt> session;
        std::shared_ptr<prepared_statement::control> statement;
        std::uint32_t automatic_close_id = 0;
        std::uint64_t automatic_close_version = 0;
        bool automatic_close = false;
        std::string request;
        std::vector<field_value> parameters;
        std::optional<std::chrono::steady_clock::time_point> expires;
        asio::steady_timer deadline;
        query_handler query_completion;
        prepare_handler prepare_completion;
        result_handler result_completion;
        query_result result;
        prepared_statement prepared;
        std::size_t remaining_columns = 0;
        std::size_t remaining_parameters = 0;
        std::size_t result_bytes = 0;
        std::size_t wire_bytes = 0;
        std::size_t queue_bytes = 0;
        bool admitted = false;
        std::size_t admission_bytes = 0;
        bool sent = false;
        bool completed = false;

        explicit command(asio::any_io_executor executor)
            : deadline(executor) {}
    };

    struct state : std::enable_shared_from_this<state>
    {
        // Destroyed last, after the resolver, timer and SSL stream.
        std::shared_ptr<arknet::detail::io_pool> pool;
        asio::any_io_executor executor;

        std::shared_ptr<attempt> current;
        std::atomic<std::uint64_t> generation{};
        std::atomic<bool> open{};
        std::atomic<std::uint64_t> ready_generation{};
        std::atomic<request_id> next_request{1};
        std::deque<std::shared_ptr<command>> pending;
        std::shared_ptr<command> active_command;
        std::size_t pending_bytes = 0;
        std::mutex admission_mutex;
        bool admission_ready = false;
        std::size_t admitted_count = 0;
        std::size_t admitted_bytes = 0;
        std::size_t admission_count_limit = 0;
        std::size_t admission_byte_limit = 0;
        std::unordered_map<std::uint32_t, std::weak_ptr<prepared_statement::control>> statements;

        explicit state(std::shared_ptr<arknet::detail::io_pool> runtime)
            : pool(std::move(runtime))
            , executor(pool->get()->executor()) {}

        bool active(const std::shared_ptr<attempt>& operation) const noexcept {
            return current == operation && !operation->completed && operation->generation == generation.load();
        }

        static connection_result failure(connection_errc code, std::string message, error_code error = {}) {
            return {code, error, 0, {}, std::move(message)};
        }

        void finish(const std::shared_ptr<attempt>& operation, connection_result result) {
            if (operation->completed)
            {
                return;
            }
            if (result && operation->generation != generation.load())
            {
                result =
                    failure(connection_errc::cancelled, "MySQL connection cancelled", asio::error::operation_aborted);
            }
            operation->completed = true;
            operation->deadline.cancel();
            if (!result)
            {
                operation->cancel();
                if (current == operation)
                {
                    open = false;
                    current.reset();
                }
            }
            else
            {
                {
                    std::lock_guard lock(admission_mutex);
                    admission_ready = operation->generation == generation.load();
                    admission_count_limit =
                        operation->options.max_pending_commands == (std::numeric_limits<std::size_t>::max)()
                            ? operation->options.max_pending_commands
                            : operation->options.max_pending_commands + 1;
                    admission_byte_limit = operation->options.max_pending_bytes;
                }
                ready_generation = operation->generation;
                open = true;
                OPENSSL_cleanse(operation->options.password.data(), operation->options.password.size());
                operation->options.password.clear();
                OPENSSL_cleanse(operation->outgoing.data(), operation->outgoing.size());
                operation->outgoing.clear();
            }
            auto handler = std::move(operation->completion);
            if (handler)
            {
                handler(std::move(result));
            }
        }

        void fail(const std::shared_ptr<attempt>& operation, connection_errc code, std::string message,
            error_code error = {}) {
            finish(operation, failure(code, std::move(message), error));
        }

        static bool contains_null(std::string_view value) noexcept {
            return value.find('\0') != std::string_view::npos;
        }

        void start(connect_options options, connect_handler handler, std::uint64_t epoch) {
            if (epoch != generation.load())
            {
                if (handler)
                {
                    handler(failure(
                        connection_errc::cancelled, "MySQL connection cancelled", asio::error::operation_aborted));
                }
                return;
            }
            if (current)
            {
                if (handler)
                {
                    handler(failure(connection_errc::already_started, "MySQL connection already active"));
                }
                return;
            }
            const auto deadline = arknet::detail::checked_deadline(std::chrono::steady_clock::now(), options.timeout);
            if (options.host.empty() || options.port == 0 || !deadline || options.max_message_size == 0 ||
                options.max_message_size > detail::default_message_limit || contains_null(options.host) ||
                contains_null(options.username) || contains_null(options.password) || contains_null(options.database) ||
                contains_null(options.server_name) || contains_null(options.ca_file))
            {
                if (handler)
                {
                    handler(failure(connection_errc::invalid_options, "Invalid MySQL connection options"));
                }
                return;
            }
            auto operation = std::make_shared<attempt>(executor, std::move(options), std::move(handler), epoch);
            current = operation;
            operation->deadline.expires_at(*deadline);
            operation->deadline.async_wait([self = shared_from_this(), operation](error_code error) {
                if (!error && self->active(operation))
                {
                    self->fail(operation, connection_errc::timeout, "MySQL connection deadline exceeded",
                        asio::error::timed_out);
                }
            });
            operation->resolver.async_resolve(operation->options.host, std::to_string(operation->options.port),
                [self = shared_from_this(), operation](
                    error_code error, asio::ip::tcp::resolver::results_type endpoints) {
                    if (!self->active(operation))
                    {
                        return;
                    }
                    if (error)
                    {
                        self->fail(operation, connection_errc::transport, "MySQL name resolution failed", error);
                        return;
                    }
                    asio::async_connect(operation->stream.next_layer(), endpoints,
                        [self, operation](error_code connect_error, const asio::ip::tcp::endpoint&) {
                            if (!self->active(operation))
                            {
                                return;
                            }
                            if (connect_error)
                            {
                                self->fail(operation, connection_errc::transport, "MySQL TCP connection failed",
                                    connect_error);
                                return;
                            }
                            self->read_message(operation, true);
                        });
                });
        }

        void read_message(const std::shared_ptr<attempt>& operation, bool greeting) {
            if (!active(operation))
            {
                return;
            }
            std::string payload;
            std::string_view input = operation->incoming;
            const auto before = input.size();
            const auto status = operation->decoder.consume(input, payload);
            operation->incoming.erase(0, before - input.size());
            if (status == detail::packet_status::complete)
            {
                operation->sequence = operation->decoder.expected_sequence();
                if (greeting)
                {
                    accept_greeting(operation, payload);
                }
                else
                {
                    accept_auth_reply(operation, payload);
                }
                return;
            }
            if (status != detail::packet_status::incomplete)
            {
                fail(operation, connection_errc::protocol,
                    status == detail::packet_status::sequence_mismatch ? "MySQL packet sequence mismatch"
                                                                       : "MySQL packet exceeds message limit");
                return;
            }
            auto handler = [self = shared_from_this(), operation, greeting](error_code error, std::size_t size) {
                if (!self->active(operation))
                {
                    return;
                }
                // Asio may report both bytes and EOF; consume any delivered
                // bytes before a subsequent read reports the terminal error.
                if (size)
                {
                    operation->incoming.append(operation->read_buffer.data(), size);
                    self->read_message(operation, greeting);
                }
                else if (error)
                {
                    self->fail(operation, connection_errc::transport, "MySQL packet read failed", error);
                }
                else
                {
                    self->fail(operation, connection_errc::protocol, "Empty MySQL transport read");
                }
            };
            if (operation->encrypted)
            {
                operation->stream.async_read_some(asio::buffer(operation->read_buffer), std::move(handler));
            }
            else
            {
                operation->stream.next_layer().async_read_some(
                    asio::buffer(operation->read_buffer), std::move(handler));
            }
        }

        void send_message(
            const std::shared_ptr<attempt>& operation, std::string_view payload, std::function<void()> continuation) {
            if (!active(operation))
            {
                return;
            }
            operation->outgoing.clear();
            if (!detail::encode_message(
                    payload, operation->sequence, operation->outgoing, operation->options.max_message_size))
            {
                fail(operation, connection_errc::protocol, "MySQL outgoing message exceeds limit");
                return;
            }
            operation->decoder.reset(operation->sequence);
            auto handler = [self = shared_from_this(), operation, continuation = std::move(continuation)](
                               error_code error, std::size_t) mutable {
                if (!self->active(operation))
                {
                    return;
                }
                if (error)
                {
                    self->fail(operation, connection_errc::transport, "MySQL packet write failed", error);
                    return;
                }
                continuation();
            };
            if (operation->encrypted)
            {
                asio::async_write(operation->stream, asio::buffer(operation->outgoing), std::move(handler));
            }
            else
            {
                asio::async_write(
                    operation->stream.next_layer(), asio::buffer(operation->outgoing), std::move(handler));
            }
        }

        bool accept_error(
            const std::shared_ptr<attempt>& operation, std::string_view payload, std::uint32_t capabilities) {
            if (payload.empty() || static_cast<unsigned char>(payload.front()) != 0xff)
            {
                return false;
            }
            detail::error_packet error;
            if (detail::parse_error(payload, capabilities, error) != detail::handshake_status::complete)
            {
                fail(operation, connection_errc::protocol, "Malformed MySQL error packet");
            }
            else
            {
                finish(operation, {capabilities ? connection_errc::authentication : connection_errc::server, {},
                                      error.code, std::move(error.sql_state), std::move(error.message)});
            }
            return true;
        }

        void accept_greeting(const std::shared_ptr<attempt>& operation, std::string_view payload) {
            if (accept_error(operation, payload, 0))
            {
                return;
            }
            if (detail::parse_greeting(payload, operation->greeting) != detail::handshake_status::complete)
            {
                fail(operation, connection_errc::protocol, "Unsupported or malformed MySQL greeting");
                return;
            }
            constexpr auto required = detail::capability::protocol_41;
            constexpr auto auth_capabilities = detail::capability::secure_connection | detail::capability::plugin_auth;
            if ((operation->greeting.capabilities & required) != required ||
                !(operation->greeting.capabilities & auth_capabilities))
            {
                fail(operation, connection_errc::protocol, "MySQL server lacks required protocol capabilities");
                return;
            }
            operation->capabilities =
                required | (operation->greeting.capabilities & (auth_capabilities | detail::capability::transactions));
            operation->auth_plugin = operation->greeting.auth_plugin_name;
            if (!operation->options.database.empty())
            {
                if (!(operation->greeting.capabilities & detail::capability::connect_with_db))
                {
                    fail(operation, connection_errc::protocol, "MySQL server cannot select initial database");
                    return;
                }
                operation->capabilities |= detail::capability::connect_with_db;
            }
            if (operation->options.tls == tls_mode::disabled)
            {
                send_handshake_response(operation);
                return;
            }
            if (!(operation->greeting.capabilities & detail::capability::ssl))
            {
                fail(operation, connection_errc::tls_required, "MySQL server does not offer required TLS");
                return;
            }
            operation->capabilities |= detail::capability::ssl;
            error_code error;
            operation->stream.set_verify_mode(asio::ssl::verify_peer, error);
            if (!error)
            {
                if (operation->options.ca_file.empty())
                {
                    operation->tls_context.set_default_verify_paths(error);
                }
                else
                {
                    operation->tls_context.load_verify_file(operation->options.ca_file, error);
                }
            }
            const auto& identity =
                operation->options.server_name.empty() ? operation->options.host : operation->options.server_name;
            if (!error)
            {
                operation->stream.set_verify_callback(asio::ssl::host_name_verification(identity), error);
            }
            if (!error && !SSL_set_min_proto_version(operation->stream.native_handle(), TLS1_2_VERSION))
            {
                error = error_code(static_cast<int>(ERR_get_error()), asio::error::get_ssl_category());
                if (!error)
                {
                    error = asio::error::operation_not_supported;
                }
            }
            error_code address_error;
            asio::ip::make_address(identity, address_error);
            if (!error && address_error &&
                !SSL_set_tlsext_host_name(operation->stream.native_handle(), identity.c_str()))
            {
                error = error_code(static_cast<int>(ERR_get_error()), asio::error::get_ssl_category());
                if (!error)
                {
                    error = asio::error::operation_not_supported;
                }
            }
            if (error)
            {
                fail(operation, connection_errc::tls, "MySQL TLS configuration failed", error);
                return;
            }
            std::string request;
            if (detail::encode_ssl_request(operation->capabilities,
                    static_cast<std::uint32_t>(operation->options.max_message_size), 45,
                    request) != detail::handshake_status::complete)
            {
                fail(operation, connection_errc::protocol, "Invalid MySQL SSLRequest");
                return;
            }
            send_message(operation, request, [self = shared_from_this(), operation] {
                operation->stream.async_handshake(asio::ssl::stream_base::client, [self, operation](error_code error) {
                    if (!self->active(operation))
                    {
                        return;
                    }
                    if (error)
                    {
                        self->fail(
                            operation, connection_errc::tls, "MySQL TLS verification or handshake failed", error);
                        return;
                    }
                    operation->encrypted = true;
                    self->send_handshake_response(operation);
                });
            });
        }

        bool make_response(
            const std::shared_ptr<attempt>& operation, std::string_view challenge, std::string& response) {
            std::string_view scramble;
            const auto normalized = detail::password_scramble(operation->auth_plugin, challenge, scramble);
            if (normalized == detail::handshake_status::unsupported)
            {
                fail(operation, connection_errc::unsupported_auth, "Unsupported MySQL authentication plugin");
                return false;
            }
            if (normalized != detail::handshake_status::complete)
            {
                fail(operation, connection_errc::protocol, "Invalid MySQL authentication challenge");
                return false;
            }
            const auto status =
                detail::make_auth_response(operation->auth_plugin, operation->options.password, scramble, response);
            if (status != detail::auth_status::complete)
            {
                fail(operation,
                    status == detail::auth_status::unsupported_plugin ? connection_errc::unsupported_auth
                                                                      : connection_errc::authentication,
                    "MySQL authentication response failed");
                return false;
            }
            return true;
        }

        void send_handshake_response(const std::shared_ptr<attempt>& operation) {
            std::string response;
            if (!make_response(operation, operation->greeting.auth_plugin_data, response))
            {
                return;
            }
            detail::handshake_response settings;
            settings.capabilities = operation->capabilities;
            settings.max_packet_size = static_cast<std::uint32_t>(operation->options.max_message_size);
            settings.username = operation->options.username;
            settings.auth_response = response;
            settings.database = operation->options.database;
            settings.auth_plugin_name = (operation->capabilities & detail::capability::plugin_auth)
                                            ? operation->auth_plugin
                                            : std::string_view{};
            std::string payload;
            if (detail::encode_handshake_response(settings, payload) != detail::handshake_status::complete)
            {
                fail(operation, connection_errc::protocol, "Invalid MySQL HandshakeResponse41");
                return;
            }
            send_message(
                operation, payload, [self = shared_from_this(), operation] { self->read_message(operation, false); });
        }

        void accept_auth_reply(const std::shared_ptr<attempt>& operation, std::string_view payload) {
            if (++operation->auth_rounds > 16 || payload.empty())
            {
                fail(operation, connection_errc::protocol, "Invalid MySQL authentication exchange");
                return;
            }
            if (accept_error(operation, payload, operation->capabilities))
            {
                return;
            }
            const auto marker = static_cast<unsigned char>(payload.front());
            if (marker == 0)
            {
                detail::ok_packet packet;
                if (detail::parse_ok(payload, operation->capabilities, packet) != detail::handshake_status::complete)
                {
                    fail(operation, connection_errc::protocol, "Malformed MySQL authentication OK packet");
                    return;
                }
                finish(operation, {});
                return;
            }
            if (operation->fast_auth_complete)
            {
                fail(operation, connection_errc::protocol, "Expected final MySQL authentication OK packet");
                return;
            }
            if (marker == 0xfe)
            {
                detail::auth_switch_request request;
                const auto status = detail::parse_auth_switch(payload, request);
                if (status != detail::handshake_status::complete)
                {
                    fail(operation,
                        status == detail::handshake_status::unsupported ? connection_errc::unsupported_auth
                                                                        : connection_errc::protocol,
                        "Invalid MySQL authentication switch");
                    return;
                }
                operation->auth_plugin = std::move(request.auth_plugin_name);
                std::string response;
                if (!make_response(operation, request.auth_plugin_data, response))
                {
                    return;
                }
                send_message(operation, response,
                    [self = shared_from_this(), operation] { self->read_message(operation, false); });
                return;
            }
            if (marker == 1 && payload.size() == 2 && operation->auth_plugin == "caching_sha2_password")
            {
                if (payload[1] == 3)
                {
                    operation->fast_auth_complete = true;
                    read_message(operation, false);
                    return;
                }
                if (payload[1] == 4)
                {
                    if (!operation->encrypted)
                    {
                        fail(operation, connection_errc::tls_required,
                            "MySQL full password authentication requires verified TLS");
                        return;
                    }
                    std::string password = operation->options.password;
                    password.push_back('\0');
                    send_message(operation, password, [self = shared_from_this(), operation] {
                        operation->fast_auth_complete = true;
                        self->read_message(operation, false);
                    });
                    OPENSSL_cleanse(password.data(), password.size());
                    return;
                }
            }
            fail(operation, connection_errc::protocol, "Unexpected MySQL authentication packet");
        }

        connection_result reserve_command(const std::shared_ptr<command>& operation) {
            std::size_t bytes = operation->request.capacity();
            const auto add = [&](std::size_t amount) {
                if (amount > detail::default_message_limit - bytes)
                {
                    return false;
                }
                bytes += amount;
                return true;
            };
            if (bytes > detail::default_message_limit)
            {
                return failure(connection_errc::queue_full, "MySQL request exceeds admission byte limit");
            }
            if (operation->parameters.capacity() > (detail::default_message_limit - bytes) / sizeof(field_value) ||
                !add(operation->parameters.capacity() * sizeof(field_value)))
            {
                return failure(connection_errc::queue_full, "MySQL parameter storage exceeds admission byte limit");
            }
            for (const auto& value : operation->parameters)
            {
                if (const auto* text = std::get_if<std::string>(&value); text && !add(text->capacity()))
                {
                    return failure(connection_errc::queue_full, "MySQL parameter storage exceeds admission byte limit");
                }
            }
            // EXECUTE/CLOSE headers are generated later on the owned lane.
            if ((operation->kind == command_kind::execute || operation->kind == command_kind::close_statement) &&
                !add(16))
            {
                return failure(connection_errc::queue_full, "MySQL request exceeds admission byte limit");
            }
            std::lock_guard lock(admission_mutex);
            if (!admission_ready || operation->generation != generation.load())
            {
                return failure(connection_errc::not_connected, "MySQL connection is not authenticated");
            }
            if (admitted_count >= admission_count_limit || admitted_bytes > admission_byte_limit ||
                bytes > admission_byte_limit - admitted_bytes)
            {
                return failure(connection_errc::queue_full, "MySQL outstanding request admission limit exceeded");
            }
            ++admitted_count;
            admitted_bytes += bytes;
            operation->admitted = true;
            operation->admission_bytes = bytes;
            return {};
        }

        void release_command(const std::shared_ptr<command>& operation) {
            std::lock_guard lock(admission_mutex);
            if (operation->admitted)
            {
                --admitted_count;
                admitted_bytes -= operation->admission_bytes;
                operation->admitted = false;
            }
        }

        void stop_admission() {
            std::lock_guard lock(admission_mutex);
            admission_ready = false;
        }

        bool command_active(const std::shared_ptr<command>& operation) const noexcept {
            return active_command == operation && !operation->completed && current == operation->session &&
                   operation->generation == generation.load() && open.load();
        }

        detail::codec_limits limits(const std::shared_ptr<command>& operation) const {
            detail::codec_limits result;
            result.max_columns = operation->options.max_columns;
            result.max_bytes = operation->options.max_result_bytes;
            return result;
        }

        void invalidate_statements() {
            for (auto& [id, weak] : statements)
            {
                if (auto statement = weak.lock())
                {
                    statement->closed = true;
                }
            }
            statements.clear();
        }

        bool draining_commands = false;

        void complete_command(const std::shared_ptr<command>& operation, connection_result result) {
            if (operation->completed)
            {
                return;
            }
            operation->completed = true;
            // Cancelled timer/transport handlers may retain this command until
            // a later executor turn. Free admitted payload storage before
            // making its producer budget available to another request.
            std::string{}.swap(operation->request);
            std::vector<field_value>{}.swap(operation->parameters);
            release_command(operation);
            operation->deadline.cancel();
            if (active_command == operation)
            {
                active_command.reset();
            }
            else
            {
                const auto position = std::find(pending.begin(), pending.end(), operation);
                if (position != pending.end())
                {
                    pending_bytes -= operation->queue_bytes;
                    pending.erase(position);
                }
            }
            if (operation->kind == command_kind::prepare)
            {
                prepare_result prepared{std::move(result), std::move(operation->prepared)};
                auto handler = std::move(operation->prepare_completion);
                if (handler)
                {
                    handler(std::move(prepared));
                }
            }
            else if (operation->kind == command_kind::close_statement || operation->kind == command_kind::reset)
            {
                auto handler = std::move(operation->result_completion);
                if (handler)
                {
                    handler(std::move(result));
                }
            }
            else
            {
                operation->result.status = std::move(result);
                auto handler = std::move(operation->query_completion);
                if (handler)
                {
                    handler(std::move(operation->result));
                }
            }
            if (!draining_commands)
            {
                start_next_command();
            }
        }

        void abort_commands(connection_result reason, bool advance_generation = true) {
            stop_admission();
            if (advance_generation)
            {
                ++generation;
            }
            open = false;
            if (current)
            {
                current->cancel();
                current.reset();
            }
            invalidate_statements();
            draining_commands = true;
            if (auto operation = active_command)
            {
                auto result = reason;
                result.outcome_unknown = operation->sent;
                complete_command(operation, std::move(result));
            }
            auto queued = std::move(pending);
            pending.clear();
            pending_bytes = 0;
            for (auto& operation : queued)
            {
                complete_command(
                    operation, failure(connection_errc::cancelled, "MySQL connection closed before command was sent",
                                   asio::error::operation_aborted));
            }
            draining_commands = false;
        }

        bool charge_result(const std::shared_ptr<command>& operation, std::size_t amount) {
            if (amount > operation->options.max_result_bytes - operation->result_bytes)
            {
                abort_commands(failure(connection_errc::result_limit, "MySQL accumulated result exceeds memory limit"));
                return false;
            }
            operation->result_bytes += amount;
            return true;
        }

        void codec_failure(detail::command_status status, std::string message) {
            if (status == detail::command_status::limit_exceeded)
            {
                abort_commands(failure(connection_errc::result_limit, std::move(message)));
            }
            else
            {
                abort_commands(failure(status == detail::command_status::unsupported ? connection_errc::unsupported
                                                                                     : connection_errc::protocol,
                    std::move(message)));
            }
        }

        bool command_error(const std::shared_ptr<command>& operation, std::string_view payload) {
            if (payload.empty() || static_cast<unsigned char>(payload.front()) != 0xff)
            {
                return false;
            }
            detail::error_packet error;
            if (detail::parse_error(payload, operation->session->capabilities, error) !=
                detail::handshake_status::complete)
            {
                abort_commands(failure(connection_errc::protocol, "Malformed MySQL command error packet"));
            }
            else
            {
                connection_result result{
                    connection_errc::server, {}, error.code, std::move(error.sql_state), std::move(error.message)};
                if (operation->kind == command_kind::prepare && operation->phase != command_phase::first)
                {
                    abort_commands(std::move(result));
                }
                else
                {
                    complete_command(operation, std::move(result));
                }
            }
            return true;
        }

        bool accept_command_status(const std::shared_ptr<command>& operation, const detail::ok_packet& packet) {
            // MULTI_RESULTS and MULTI_STATEMENTS are deliberately not negotiated.
            if (packet.status_flags & 8)
            {
                abort_commands(failure(connection_errc::unsupported, "Unexpected MySQL multiple-result response"));
                return false;
            }
            operation->result.affected_rows = packet.affected_rows;
            operation->result.last_insert_id = packet.last_insert_id;
            operation->result.status_flags = packet.status_flags;
            operation->result.warnings = packet.warnings;
            return true;
        }

        void read_command(const std::shared_ptr<command>& operation) {
            if (!command_active(operation))
            {
                return;
            }
            auto& session = *operation->session;
            std::string payload;
            std::string_view input = session.incoming;
            const auto before = input.size();
            const auto status = session.decoder.consume(input, payload);
            session.incoming.erase(0, before - input.size());
            if (status == detail::packet_status::complete)
            {
                session.sequence = session.decoder.expected_sequence();
                if (payload.size() > operation->options.max_result_bytes - operation->wire_bytes)
                {
                    abort_commands(
                        failure(connection_errc::result_limit, "MySQL accumulated response exceeds byte limit"));
                    return;
                }
                operation->wire_bytes += payload.size();
                accept_command_packet(operation, payload);
                return;
            }
            if (status != detail::packet_status::incomplete)
            {
                abort_commands(
                    failure(status == detail::packet_status::message_too_large ? connection_errc::result_limit
                                                                               : connection_errc::protocol,
                        "Invalid MySQL command packet length or sequence"));
                return;
            }
            auto handler = [self = shared_from_this(), operation](error_code error, std::size_t count) {
                if (!self->command_active(operation))
                {
                    return;
                }
                if (count)
                {
                    operation->session->incoming.append(operation->session->read_buffer.data(), count);
                    self->read_command(operation);
                }
                else
                {
                    self->abort_commands(failure(connection_errc::transport, "MySQL command read failed",
                        error ? error : error_code(asio::error::eof)));
                }
            };
            if (session.encrypted)
            {
                session.stream.async_read_some(asio::buffer(session.read_buffer), std::move(handler));
            }
            else
            {
                session.stream.next_layer().async_read_some(asio::buffer(session.read_buffer), std::move(handler));
            }
        }

        void finish_preparation(const std::shared_ptr<command>& operation) {
            auto record = operation->prepared.control_;
            const auto id = record->id;
            const auto epoch = record->generation;
            const auto version = record->session_version;
            record->cleanup = [weak = weak_from_this(), id, epoch, version] {
                if (auto self = weak.lock())
                {
                    asio::post(self->executor,
                        [self, id, epoch, version] { self->automatic_statement_close(id, epoch, version); });
                }
            };
            complete_command(operation, {});
        }

        void accept_prepare_packet(const std::shared_ptr<command>& operation, std::string_view payload) {
            if (operation->phase == command_phase::first)
            {
                detail::prepared_statement_ok response;
                const auto status =
                    detail::parse_prepare_ok(payload, response, limits(operation), operation->session->capabilities);
                if (status != detail::command_status::complete)
                {
                    codec_failure(status, "Invalid MySQL prepare response");
                    return;
                }
                auto record = std::make_shared<prepared_statement::control>();
                record->owner = shared_from_this();
                record->generation = operation->generation;
                record->session_version = operation->session->session_version;
                record->id = response.statement_id;
                record->parameter_count = response.parameter_count;
                if (const auto previous = statements.find(record->id);
                    previous != statements.end() && !previous->second.expired())
                {
                    abort_commands(failure(connection_errc::protocol, "MySQL reused an active prepared statement ID"));
                    return;
                }
                operation->prepared.control_ = record;
                statements[record->id] = record;
                operation->remaining_parameters = response.parameter_count;
                operation->remaining_columns = response.column_count;
                if (!charge_result(operation, sizeof(prepared_statement::control)))
                {
                    return;
                }
                if (operation->remaining_parameters)
                {
                    operation->phase = command_phase::prepare_parameters;
                }
                else if (operation->remaining_columns)
                {
                    operation->phase = command_phase::prepare_columns;
                }
                else
                {
                    finish_preparation(operation);
                    return;
                }
            }
            else if (operation->phase == command_phase::prepare_parameters ||
                     operation->phase == command_phase::prepare_columns)
            {
                column_definition column;
                const auto status = detail::parse_column(payload, column, limits(operation));
                if (status != detail::command_status::complete)
                {
                    codec_failure(status, "Invalid MySQL prepared metadata");
                    return;
                }
                if (operation->phase == command_phase::prepare_parameters)
                {
                    if (!--operation->remaining_parameters)
                    {
                        operation->phase = command_phase::prepare_parameter_end;
                    }
                }
                else
                {
                    if (!charge_result(operation, sizeof(column_definition) + payload.size()))
                    {
                        return;
                    }
                    operation->prepared.control_->columns.push_back(std::move(column));
                    if (!--operation->remaining_columns)
                    {
                        operation->phase = command_phase::prepare_column_end;
                    }
                }
            }
            else
            {
                detail::ok_packet eof;
                const auto status = detail::parse_eof(payload, eof, limits(operation));
                if (status != detail::command_status::complete)
                {
                    codec_failure(status, "Invalid MySQL prepared metadata terminator");
                    return;
                }
                if (operation->phase == command_phase::prepare_parameter_end && operation->remaining_columns)
                {
                    operation->phase = command_phase::prepare_columns;
                }
                else
                {
                    finish_preparation(operation);
                    return;
                }
            }
            asio::post(executor, [self = shared_from_this(), operation] { self->read_command(operation); });
        }

        void accept_command_packet(const std::shared_ptr<command>& operation, std::string_view payload) {
            if (command_error(operation, payload))
            {
                return;
            }
            if (operation->kind == command_kind::prepare)
            {
                accept_prepare_packet(operation, payload);
                return;
            }
            if (operation->phase == command_phase::first)
            {
                if (!payload.empty() && payload.front() == 0)
                {
                    detail::ok_packet packet;
                    if (detail::parse_ok(payload, operation->session->capabilities, packet) !=
                        detail::handshake_status::complete)
                    {
                        abort_commands(failure(connection_errc::protocol, "Malformed MySQL command OK packet"));
                        return;
                    }
                    if (!accept_command_status(operation, packet))
                    {
                        return;
                    }
                    if (operation->kind == command_kind::reset)
                    {
                        ++operation->session->session_version;
                        invalidate_statements();
                    }
                    complete_command(operation, {});
                    return;
                }
                if (operation->kind == command_kind::reset)
                {
                    abort_commands(failure(connection_errc::protocol, "Expected MySQL reset OK packet"));
                    return;
                }
                if (!payload.empty() && static_cast<unsigned char>(payload.front()) == 0xfb)
                {
                    abort_commands(failure(connection_errc::unsupported, "MySQL LOCAL INFILE is disabled"));
                    return;
                }
                const auto status = detail::parse_column_count(
                    payload, operation->remaining_columns, limits(operation), operation->session->capabilities);
                if (status != detail::command_status::complete)
                {
                    codec_failure(status, "Invalid MySQL result column count");
                    return;
                }
                operation->phase = command_phase::columns;
            }
            else if (operation->phase == command_phase::columns)
            {
                column_definition column;
                const auto status = detail::parse_column(payload, column, limits(operation));
                if (status != detail::command_status::complete)
                {
                    codec_failure(status, "Invalid MySQL column definition");
                    return;
                }
                if (!charge_result(operation, sizeof(column_definition) + payload.size()))
                {
                    return;
                }
                operation->result.columns.push_back(std::move(column));
                if (!--operation->remaining_columns)
                {
                    operation->phase = command_phase::column_end;
                }
            }
            else if (operation->phase == command_phase::column_end ||
                     (operation->phase == command_phase::rows && !payload.empty() &&
                         static_cast<unsigned char>(payload.front()) == 0xfe && payload.size() < 9))
            {
                detail::ok_packet packet;
                const auto status = detail::parse_eof(payload, packet, limits(operation));
                if (status != detail::command_status::complete)
                {
                    codec_failure(status, "Invalid MySQL result terminator");
                    return;
                }
                if (!accept_command_status(operation, packet))
                {
                    return;
                }
                if (operation->phase == command_phase::rows)
                {
                    complete_command(operation, {});
                    return;
                }
                operation->phase = command_phase::rows;
            }
            else
            {
                if (operation->result.rows.size() >= operation->options.max_rows ||
                    !charge_result(operation,
                        sizeof(row_values) + operation->result.columns.size() * sizeof(field_value) + payload.size()))
                {
                    if (command_active(operation))
                    {
                        abort_commands(failure(connection_errc::result_limit, "MySQL result exceeds row limit"));
                    }
                    return;
                }
                row_values row;
                const auto status =
                    operation->kind == command_kind::execute
                        ? detail::parse_binary_row(payload, operation->result.columns, row, limits(operation))
                        : detail::parse_text_row(payload, operation->result.columns, row, limits(operation));
                if (status != detail::command_status::complete)
                {
                    codec_failure(status, "Invalid MySQL result row");
                    return;
                }
                operation->result.rows.push_back(std::move(row));
            }
            asio::post(executor, [self = shared_from_this(), operation] { self->read_command(operation); });
        }

        bool valid_statement(const std::shared_ptr<command>& operation) const {
            const auto& statement = operation->statement;
            return statement && !statement->closed.load() && statement->owner.lock().get() == this &&
                   statement->generation == operation->generation &&
                   statement->session_version == operation->session->session_version;
        }

        void start_next_command() {
            if (draining_commands || active_command || pending.empty())
            {
                return;
            }
            auto operation = pending.front();
            pending.pop_front();
            pending_bytes -= operation->queue_bytes;
            active_command = operation;
            if (!current || !open || operation->generation != generation.load() || current != operation->session)
            {
                complete_command(operation, failure(connection_errc::not_connected, "MySQL connection is not ready"));
                return;
            }
            if (std::chrono::steady_clock::now() >= *operation->expires)
            {
                complete_command(operation,
                    failure(connection_errc::timeout, "MySQL command expired before sending", asio::error::timed_out));
                return;
            }
            if ((operation->kind == command_kind::execute || operation->kind == command_kind::close_statement) &&
                !operation->automatic_close)
            {
                if (!valid_statement(operation))
                {
                    complete_command(operation, failure(connection_errc::invalid_statement,
                                                    "MySQL statement belongs to a closed or different session"));
                    return;
                }
                if (operation->kind == command_kind::execute &&
                    operation->parameters.size() != operation->statement->parameter_count)
                {
                    complete_command(operation,
                        failure(connection_errc::invalid_options, "MySQL statement parameter count mismatch"));
                    return;
                }
            }
            if (operation->automatic_close && operation->automatic_close_version != current->session_version)
            {
                complete_command(operation, {});
                return;
            }
            auto request_limits = limits(operation);
            request_limits.max_bytes = current->options.max_message_size;
            detail::command_status encoded = detail::command_status::complete;
            if (operation->kind == command_kind::execute)
            {
                encoded = detail::encode_execute(
                    operation->statement->id, operation->parameters, operation->request, request_limits);
            }
            else if (operation->kind == command_kind::close_statement)
            {
                const auto id = operation->automatic_close ? operation->automatic_close_id : operation->statement->id;
                encoded = detail::encode_close(id, operation->request, request_limits);
                if (operation->statement)
                {
                    operation->statement->closed = true;
                }
                statements.erase(id);
            }
            if (encoded != detail::command_status::complete)
            {
                complete_command(operation,
                    failure(connection_errc::invalid_options, "Invalid MySQL statement parameters or request size"));
                return;
            }
            current->sequence = 0;
            current->outgoing.clear();
            if (!current->incoming.empty() || current->decoder.buffered_size() != 0)
            {
                abort_commands(
                    failure(connection_errc::protocol, "Unexpected buffered MySQL bytes at command boundary"));
                return;
            }
            if (!detail::encode_message(
                    operation->request, current->sequence, current->outgoing, current->options.max_message_size))
            {
                complete_command(
                    operation, failure(connection_errc::invalid_options, "MySQL command exceeds message size limit"));
                return;
            }
            current->decoder = detail::packet_decoder(
                (std::min)(current->options.max_message_size, operation->options.max_result_bytes), current->sequence);
            auto handler = [self = shared_from_this(), operation](error_code error, std::size_t) {
                if (!self->command_active(operation))
                {
                    return;
                }
                if (error)
                {
                    self->abort_commands(failure(connection_errc::transport, "MySQL command write failed", error));
                }
                else if (operation->kind == command_kind::close_statement)
                {
                    self->complete_command(operation, {});
                }
                else
                {
                    self->read_command(operation);
                }
            };
            operation->sent = true;
            if (current->encrypted)
            {
                asio::async_write(current->stream, asio::buffer(current->outgoing), std::move(handler));
            }
            else
            {
                asio::async_write(current->stream.next_layer(), asio::buffer(current->outgoing), std::move(handler));
            }
        }

        void enqueue_command(const std::shared_ptr<command>& operation) {
            if (operation->generation != generation.load())
            {
                complete_command(
                    operation, failure(connection_errc::cancelled, "MySQL command cancelled before submission",
                                   asio::error::operation_aborted));
                return;
            }
            if (!current || !open || !current->completed)
            {
                complete_command(
                    operation, failure(connection_errc::not_connected, "MySQL connection is not authenticated"));
                return;
            }
            operation->session = current;
            if (!operation->expires || !operation->options.max_columns || operation->options.max_columns > 65535 ||
                !operation->options.max_result_bytes ||
                operation->options.max_result_bytes > detail::default_message_limit)
            {
                complete_command(operation, failure(connection_errc::invalid_options, "Invalid MySQL query options"));
                return;
            }
            operation->queue_bytes = operation->request.size();
            for (const auto& value : operation->parameters)
            {
                if (operation->queue_bytes > current->options.max_pending_bytes ||
                    sizeof(field_value) > current->options.max_pending_bytes - operation->queue_bytes)
                {
                    operation->queue_bytes = current->options.max_pending_bytes;
                    complete_command(operation,
                        failure(connection_errc::queue_full, "MySQL command parameters exceed queue byte limit"));
                    return;
                }
                operation->queue_bytes += sizeof(field_value);
                if (const auto* text = std::get_if<std::string>(&value))
                {
                    if (text->size() > current->options.max_pending_bytes - operation->queue_bytes)
                    {
                        complete_command(operation,
                            failure(connection_errc::queue_full, "MySQL command parameters exceed queue byte limit"));
                        return;
                    }
                    operation->queue_bytes += text->size();
                }
            }
            if ((active_command && pending.size() >= current->options.max_pending_commands) ||
                operation->queue_bytes > current->options.max_pending_bytes - pending_bytes)
            {
                complete_command(
                    operation, failure(connection_errc::queue_full, "MySQL pending command queue limit exceeded"));
                return;
            }
            operation->deadline.expires_at(*operation->expires);
            operation->deadline.async_wait([self = shared_from_this(), operation](error_code error) {
                if (error || operation->completed)
                {
                    return;
                }
                if (self->active_command == operation)
                {
                    self->abort_commands(
                        failure(connection_errc::timeout, "MySQL command deadline exceeded", asio::error::timed_out));
                }
                else
                {
                    self->complete_command(operation,
                        failure(connection_errc::timeout, "MySQL command expired in queue", asio::error::timed_out));
                }
            });
            pending.push_back(operation);
            pending_bytes += operation->queue_bytes;
            start_next_command();
        }

        void automatic_statement_close(std::uint32_t id, std::uint64_t epoch, std::uint64_t version) {
            if (!current || !open || epoch != generation.load() || version != current->session_version)
            {
                return;
            }
            auto operation = std::make_shared<command>(executor);
            operation->id = next_request.fetch_add(1);
            operation->kind = command_kind::close_statement;
            operation->generation = epoch;
            operation->expires =
                arknet::detail::checked_deadline(std::chrono::steady_clock::now(), operation->options.timeout);
            operation->automatic_close = true;
            operation->automatic_close_id = id;
            operation->automatic_close_version = version;
            operation->result_completion = [weak = weak_from_this()](connection_result result) {
                if (!result && result.code == connection_errc::queue_full)
                {
                    if (auto self = weak.lock())
                    {
                        self->abort_commands(failure(
                            connection_errc::queue_full, "MySQL automatic statement cleanup exceeded queue limit"));
                    }
                }
            };
            const auto admission = reserve_command(operation);
            if (!admission)
            {
                complete_command(operation, admission);
            }
            else
            {
                enqueue_command(operation);
            }
        }

        void cancel_command(request_id id) {
            if (active_command && active_command->id == id)
            {
                abort_commands(failure(connection_errc::cancelled, "MySQL command cancelled after sending",
                    asio::error::operation_aborted));
                return;
            }
            const auto position = std::find_if(
                pending.begin(), pending.end(), [id](const auto& operation) { return operation->id == id; });
            if (position != pending.end())
            {
                auto operation = *position;
                complete_command(operation, failure(connection_errc::cancelled, "MySQL queued command cancelled",
                                                asio::error::operation_aborted));
            }
        }

        void request_close() {
            const auto self = shared_from_this();
            const auto cutoff = generation.fetch_add(1) + 1;
            stop_admission();
            open = false;
            asio::post(executor, [self, cutoff] {
                if (!self->current || self->current->generation >= cutoff)
                {
                    return;
                }
                auto operation = self->current;
                if (self->active_command || !self->pending.empty())
                {
                    self->abort_commands(
                        failure(connection_errc::cancelled, "MySQL connection closed", asio::error::operation_aborted),
                        false);
                }
                self->invalidate_statements();
                operation->cancel();
                if (!operation->completed)
                {
                    self->fail(operation, connection_errc::cancelled, "MySQL connection cancelled",
                        asio::error::operation_aborted);
                }
                else
                {
                    self->current.reset();
                }
            });
        }
    };

    std::shared_ptr<state> state_;

    std::shared_ptr<command> make_command(command_kind kind, query_options options) {
        auto operation = std::make_shared<command>(state_->executor);
        operation->id = state_->next_request.fetch_add(1);
        operation->kind = kind;
        operation->options = options;
        operation->generation = state_->generation.load();
        operation->expires =
            arknet::detail::checked_deadline(std::chrono::steady_clock::now(), operation->options.timeout);
        return operation;
    }

    request_id submit_command(
        std::shared_ptr<command> request, detail::command_status encoded = detail::command_status::complete) {
        const auto self = state_;
        // Destroy the command's timer before releasing this runtime owner.
        auto operation = std::move(request);
        const auto id = operation->id;
        auto admission =
            !operation->expires ? state::failure(connection_errc::invalid_options, "Invalid MySQL command deadline")
            : encoded == detail::command_status::complete
                ? self->reserve_command(operation)
                : state::failure(connection_errc::invalid_options, "Invalid MySQL command payload or size");
        if (!admission)
        {
            std::string{}.swap(operation->request);
            std::vector<field_value>{}.swap(operation->parameters);
            operation->statement.reset();
        }
        try
        {
            asio::post(self->executor, [self, operation, admission = std::move(admission)]() mutable {
                if (!admission)
                {
                    self->complete_command(operation, std::move(admission));
                }
                else
                {
                    self->enqueue_command(operation);
                }
            });
        }
        catch (...)
        {
            self->release_command(operation);
            throw;
        }
        return id;
    }

    explicit connection(std::shared_ptr<arknet::detail::io_pool> runtime)
        : state_(std::make_shared<state>(std::move(runtime))) {
        if (!state_->pool->start())
        {
            throw std::runtime_error("Cannot start MySQL IO runtime");
        }
    }

public:
    connection()
        : connection(std::shared_ptr<arknet::detail::io_pool>(
              new arknet::detail::io_pool(1), arknet::detail::iopool_deleter{})) {}

    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;
    connection(connection&&) = delete;
    connection& operator=(connection&&) = delete;

    ~connection() {
        close();
    }

    void async_connect(connect_options options, connect_handler handler) {
        // A completion on another thread may destroy the facade before post returns.
        const auto self = state_;
        const auto epoch = self->generation.load();
        asio::post(self->executor, [self, options = std::move(options), handler = std::move(handler), epoch]() mutable {
            self->start(std::move(options), std::move(handler), epoch);
        });
    }

    // Requests are FIFO on this connection. A timeout includes queue time.
    // Cancelling a sent request closes the connection; the server may already
    // have executed it (status.outcome_unknown). Requests are never replayed.
    request_id async_query(std::string sql, query_handler handler, query_options options = {}) {
        auto operation = make_command(command_kind::query, options);
        operation->query_completion = std::move(handler);
        const auto encoded = detail::encode_query(sql, operation->request);
        return submit_command(std::move(operation), encoded);
    }

    request_id async_prepare(std::string sql, prepare_handler handler, query_options options = {}) {
        auto operation = make_command(command_kind::prepare, options);
        operation->prepare_completion = std::move(handler);
        const auto encoded = detail::encode_prepare(sql, operation->request);
        return submit_command(std::move(operation), encoded);
    }

    request_id async_execute(const prepared_statement& statement, std::vector<field_value> parameters,
        query_handler handler, query_options options = {}) {
        auto operation = make_command(command_kind::execute, options);
        operation->statement = statement.control_;
        operation->parameters = std::move(parameters);
        operation->query_completion = std::move(handler);
        return submit_command(std::move(operation));
    }

    request_id async_close(
        const prepared_statement& statement, result_handler handler = {}, query_options options = {}) {
        auto operation = make_command(command_kind::close_statement, options);
        operation->statement = statement.control_;
        operation->result_completion = std::move(handler);
        return submit_command(std::move(operation));
    }

    // The caller owns transaction ordering when sharing a bare connection.
    // A connection-pool lease can instead provide exclusive ownership.
    request_id async_begin(query_handler handler, query_options options = {}) {
        return async_query("START TRANSACTION", std::move(handler), options);
    }

    request_id async_commit(query_handler handler, query_options options = {}) {
        return async_query("COMMIT", std::move(handler), options);
    }

    request_id async_rollback(query_handler handler, query_options options = {}) {
        return async_query("ROLLBACK", std::move(handler), options);
    }

    // COM_RESET_CONNECTION performs a real server-side session reset. Success
    // invalidates all prepared handles and releases transaction/session state.
    request_id async_reset(result_handler handler, query_options options = {}) {
        auto operation = make_command(command_kind::reset, options);
        operation->request.assign(1, '\x1f');
        operation->result_completion = std::move(handler);
        return submit_command(std::move(operation));
    }

    void cancel(request_id id) {
        const auto self = state_;
        asio::post(self->executor, [self, id] { self->cancel_command(id); });
    }

    void close() {
        const auto self = state_;
        self->request_close();
    }

    bool is_open() const noexcept {
        return state_->open.load() && state_->ready_generation.load() == state_->generation.load();
    }
};

} // namespace arknet::mysql
