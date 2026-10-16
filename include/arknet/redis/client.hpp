// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arknet/base/detail/deadline.hpp>
#include <arknet/redis/detail/resp.hpp>
#include <arknet/tcp/tcp_client.hpp>

#if defined(ARKNET_ENABLE_SSL)
#include <arknet/tcp/tcps_client.hpp>

#endif

namespace arknet::redis {

using value = detail::resp_value;
using value_type = detail::resp_type;
using command = std::vector<std::string>;
using request_id = std::uint64_t;

enum class protocol_version
{
    resp2,
    resp3
};

enum class client_errc
{
    none,
    not_connected,
    already_started,
    invalid_command,
    unsupported_command,
    queue_full,
    timeout,
    cancelled,
    disconnected,
    connection_failed,
    handshake_failed,
    protocol_error,
    response_limit,
    closed
};

struct result
{
    client_errc error{client_errc::none};
    arknet::error_code transport_error;
    std::vector<value> replies;
    // On failure, true means a sent command may have executed. Do not automatically retry writes.
    bool command_may_have_run{};

    explicit operator bool() const noexcept {
        return error == client_errc::none;
    }
};

struct connect_options
{
    std::string host{"127.0.0.1"};
    std::string port{"6379"};
    protocol_version protocol{protocol_version::resp3};
    std::string username;
    std::string password;
    std::string client_name;
    std::uint32_t database{};
};

struct client_options
{
    std::size_t max_pending_commands{1024};
    std::size_t max_pending_bytes{16 * 1024 * 1024};
    std::size_t max_pipeline_response_bytes{64 * 1024 * 1024};
    std::chrono::milliseconds connect_timeout{5000};
    std::chrono::milliseconds command_timeout{5000};
    // A cancelled sent command retains its response slot until drained. Bound that waiting time too.
    std::chrono::milliseconds drain_timeout{5000};
    detail::resp_limits response_limits;
    std::string tls_certificate_authority;
    std::string tls_certificate_file;
    std::string tls_private_key_file;
    std::string tls_private_key_password;
};

template<class Transport>
class basic_pool;

// One connection, one ordered request stream. No automatic reconnect or command replay.
// close() is terminal; construct another client to connect again. Handlers must not throw.
// Completions and pushes run on the connection IO lane, except synchronous
// rejection after close_and_wait() closes the permanent submission gate.
// Pub/Sub, replication, MONITOR and CLIENT REPLY OFF/SKIP require other response models and are rejected.
template<class Transport>
class basic_client
{
public:
    using completion_handler = std::function<void(result)>;
    using push_handler = std::function<void(value)>;

private:
    struct request
    {
        request_id id{};
        std::size_t remaining{};
        std::size_t commands{};
        std::size_t wire_bytes{};
        std::size_t response_bytes{};
        bool sent{};
        bool completed{};
        bool handshake{};
        bool reserved{};
        std::vector<value> replies;
        completion_handler handler;
        std::shared_ptr<asio::steady_timer> timer;
    };

    struct state : std::enable_shared_from_this<state>
    {
        client_options options;
        const bool owns_runtime;
        std::shared_ptr<arknet::detail::io_pool> runtime;
        std::shared_ptr<Transport> transport;
        detail::resp_decoder decoder;
        std::deque<std::shared_ptr<request>> slots;
        completion_handler connect_handler;
        push_handler push;
        std::shared_ptr<asio::steady_timer> connect_timer;
        connect_options connection;
        std::atomic_bool ready{};
        std::atomic_bool close_requested{};
        std::atomic_bool close_started{};
        std::atomic<request_id> next_id{1};
        bool started{};
        bool terminal{};
        bool native_cleanup_pending{};
        std::exception_ptr native_cleanup_failure;
        std::mutex admission_mutex;
        std::size_t pending_commands{};
        std::size_t pending_bytes{};
        std::mutex publishing_mutex;
        std::condition_variable publishing_changed;
        std::size_t publishers{};
        bool terminating{};
        bool terminated{};
        std::thread::id finalizer;
        std::exception_ptr shutdown_failure;

        explicit state(client_options configuration, const std::string& certificate_authority,
            std::shared_ptr<arknet::detail::io_pool> scheduler = {})
            : options(configuration)
            , owns_runtime(!scheduler)
            , runtime(scheduler ? std::move(scheduler)
                                : std::shared_ptr<arknet::detail::io_pool>(new arknet::detail::io_pool(1),
                                      [](arknet::detail::io_pool* pool) { arknet::detail::iopool_deleter{}(pool); }))
            , transport(std::make_shared<Transport>(*runtime))
            , decoder(options.response_limits) {
            const auto now = std::chrono::steady_clock::now();
            if (!options.max_pending_commands || !options.max_pending_bytes || !options.max_pipeline_response_bytes ||
                !arknet::detail::checked_deadline(now, options.connect_timeout) ||
                !arknet::detail::checked_deadline(now, options.command_timeout) ||
                !arknet::detail::checked_deadline(now, options.drain_timeout))
            {
                throw std::invalid_argument("Redis queue limits and timeouts must be positive and representable");
            }
            const auto& authority =
                certificate_authority.empty() ? options.tls_certificate_authority : certificate_authority;
            if (!options.tls_certificate_file.empty() || !options.tls_private_key_file.empty())
            {
                if constexpr (requires {
                                  transport->set_cert_file(authority, options.tls_certificate_file,
                                      options.tls_private_key_file, options.tls_private_key_password);
                              })
                {
                    transport->set_cert_file(authority, options.tls_certificate_file, options.tls_private_key_file,
                        options.tls_private_key_password);
                    if (auto error = arknet::get_last_error())
                    {
                        throw arknet::system_error(error);
                    }
                }
                else
                {
                    throw std::invalid_argument("Client certificates require a Redis TLS client");
                }
            }
            else if (!authority.empty())
            {
                if constexpr (requires { transport->load_verify_file(authority); })
                {
                    transport->load_verify_file(authority);
                }
                else
                {
                    throw std::invalid_argument("A certificate authority requires a Redis TLS client");
                }
            }
            transport->set_auto_reconnect(false);
        }

        class publication
        {
            inline static thread_local publication* current_{};
            state* owner_{};
            publication* previous_{};

        public:
            explicit publication(state& owner) {
                std::lock_guard lock(owner.publishing_mutex);
                if (!owner.terminating)
                {
                    ++owner.publishers;
                    owner_ = &owner;
                    previous_ = current_;
                    current_ = this;
                }
            }

            publication(const publication&) = delete;
            publication& operator=(const publication&) = delete;

            ~publication() {
                if (owner_)
                {
                    current_ = previous_;
                    {
                        std::lock_guard lock(owner_->publishing_mutex);
                        --owner_->publishers;
                    }
                    owner_->publishing_changed.notify_all();
                }
            }

            explicit operator bool() const noexcept {
                return owner_ != nullptr;
            }

            static bool active_on_this_thread(const state& owner) noexcept {
                for (auto* current = current_; current; current = current->previous_)
                {
                    if (current->owner_ == &owner)
                    {
                        return true;
                    }
                }
                return false;
            }
        };

        auto executor() const {
            return transport->io().executor();
        }

        void initialize() {
            auto weak = this->weak_from_this();
            transport->bind_connect([weak] {
                if (auto self = weak.lock())
                {
                    self->connected(arknet::get_last_error());
                }
            });
            transport->bind_recv([weak](std::string_view bytes) {
                if (auto self = weak.lock())
                {
                    self->received(bytes);
                }
            });
            transport->bind_disconnect([weak] {
                if (auto self = weak.lock())
                {
                    self->shutdown(client_errc::disconnected, arknet::get_last_error());
                }
            });
            // Keep rejection/cancellation callbacks runnable even before connect and after close.
            arknet::detail::pool_cleanup();
            if (!runtime->start())
            {
                throw arknet::system_error(arknet::get_last_error());
            }
        }

        static std::string encode(const std::vector<command>& commands, std::size_t maximum) {
            if (commands.empty())
            {
                throw std::invalid_argument("A Redis pipeline requires at least one command");
            }
            std::string bytes;
            for (const auto& arguments : commands)
            {
                if (!arguments.empty() && unsupported(arguments))
                {
                    throw std::domain_error("Redis command requires a different response model");
                }
                std::vector<std::string_view> views(arguments.begin(), arguments.end());
                bytes += detail::encode_command(views, maximum - bytes.size());
            }
            return bytes;
        }

        static bool matches(std::string_view left, std::string_view right) {
            if (left.size() != right.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < left.size(); ++i)
            {
                auto character = left[i];
                if (character >= 'a' && character <= 'z')
                {
                    character = static_cast<char>(character - 'a' + 'A');
                }
                if (character != right[i])
                {
                    return false;
                }
            }
            return true;
        }

        static bool unsupported(const command& arguments) {
            for (std::string_view name : {"SUBSCRIBE", "PSUBSCRIBE", "SSUBSCRIBE", "UNSUBSCRIBE", "PUNSUBSCRIBE",
                     "SUNSUBSCRIBE", "MONITOR", "SYNC", "PSYNC", "REPLCONF"})
            {
                if (matches(arguments.front(), name))
                {
                    return true;
                }
            }
            return arguments.size() >= 3 && matches(arguments[0], "CLIENT") && matches(arguments[1], "REPLY") &&
                   (matches(arguments[2], "OFF") || matches(arguments[2], "SKIP"));
        }

        bool reserve(const std::shared_ptr<request>& operation) {
            std::lock_guard lock(admission_mutex);
            if (operation->commands > options.max_pending_commands - pending_commands ||
                operation->wire_bytes > options.max_pending_bytes - pending_bytes)
            {
                return false;
            }
            pending_commands += operation->commands;
            pending_bytes += operation->wire_bytes;
            operation->reserved = true;
            return true;
        }

        void release(const std::shared_ptr<request>& operation) {
            std::lock_guard lock(admission_mutex);
            if (operation->reserved)
            {
                operation->reserved = false;
                pending_commands -= operation->commands;
                pending_bytes -= operation->wire_bytes;
            }
        }

        static void cancel_timer(const std::shared_ptr<asio::steady_timer>& timer) {
            if (timer)
            {
                timer->cancel();
            }
        }

        void complete(std::shared_ptr<request> operation, result response) {
            if (operation->completed)
            {
                return;
            }
            operation->completed = true;
            cancel_timer(operation->timer);
            auto handler = std::exchange(operation->handler, {});
            operation->replies.clear();
            if (handler)
            {
                handler(std::move(response));
            }
        }

        void connect(
            connect_options target, completion_handler handler, std::chrono::steady_clock::time_point deadline) {
            if (terminal || close_requested)
            {
                if (handler)
                {
                    handler(result{client_errc::closed});
                }
                return;
            }
            if (started)
            {
                if (handler)
                {
                    handler(result{client_errc::already_started});
                }
                return;
            }
            if (target.host.empty() || target.port.empty())
            {
                if (handler)
                {
                    handler(result{client_errc::invalid_command});
                }
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                if (handler)
                {
                    handler(result{client_errc::timeout});
                }
                return;
            }
            started = true;
            connection = std::move(target);
            connect_handler = std::move(handler);
            connect_timer = std::make_shared<asio::steady_timer>(executor());
            connect_timer->expires_at(deadline);
            connect_timer->async_wait([weak = this->weak_from_this()](arknet::error_code error) {
                if (!error)
                {
                    if (auto self = weak.lock())
                    {
                        self->shutdown(client_errc::timeout);
                    }
                }
            });
            transport->set_connect_timeout(options.connect_timeout);
            if (!transport->async_start(connection.host, connection.port))
            {
                shutdown(client_errc::connection_failed, arknet::get_last_error());
            }
        }

        void connected(arknet::error_code error) {
            if (terminal)
            {
                // The transport connect event can finish after Redis shutdown
                // closed an earlier socket. Retire that late-opened transport
                // without completing the Redis request a second time.
                cleanup_transport();
                return;
            }
            if (close_requested)
            {
                shutdown(client_errc::closed);
                return;
            }
            if (error)
            {
                shutdown(client_errc::connection_failed, error);
                return;
            }
            std::vector<command> handshake;
            if (connection.protocol == protocol_version::resp3)
            {
                command hello{"HELLO", "3"};
                if (!connection.password.empty() || !connection.username.empty())
                {
                    hello.insert(hello.end(),
                        {"AUTH", connection.username.empty() ? "default" : connection.username, connection.password});
                }
                if (!connection.client_name.empty())
                {
                    hello.insert(hello.end(), {"SETNAME", connection.client_name});
                }
                handshake.push_back(std::move(hello));
            }
            else
            {
                if (!connection.password.empty() || !connection.username.empty())
                {
                    command authenticate{"AUTH"};
                    if (!connection.username.empty())
                    {
                        authenticate.push_back(connection.username);
                    }
                    authenticate.push_back(connection.password);
                    handshake.push_back(std::move(authenticate));
                }
                if (!connection.client_name.empty())
                {
                    handshake.push_back({"CLIENT", "SETNAME", connection.client_name});
                }
                handshake.push_back({"PING"});
            }
            if (connection.database)
            {
                handshake.push_back({"SELECT", std::to_string(connection.database)});
            }
            auto operation = std::make_shared<request>();
            operation->handshake = true;
            operation->commands = operation->remaining = handshake.size();
            try
            {
                auto bytes = encode(handshake, options.max_pending_bytes);
                enqueue(operation, std::move(bytes));
            }
            catch (const std::exception&)
            { shutdown(client_errc::invalid_command); }
        }

        void submit(
            std::shared_ptr<request> operation, std::string bytes, std::chrono::steady_clock::time_point deadline) {
            if (terminal || close_requested || !ready)
            {
                release(operation);
                complete(
                    operation, result{terminal || close_requested ? client_errc::closed : client_errc::not_connected});
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                release(operation);
                complete(operation, result{client_errc::timeout});
                return;
            }
            operation->timer = std::make_shared<asio::steady_timer>(executor());
            operation->timer->expires_at(deadline);
            operation->timer->async_wait(
                [weak = this->weak_from_this(), operation = std::weak_ptr(operation)](arknet::error_code error) {
                    if (!error)
                    {
                        if (auto self = weak.lock())
                        {
                            if (auto current = operation.lock())
                            {
                                self->cancel_request(current, client_errc::timeout);
                            }
                        }
                    }
                });
            enqueue(operation, std::move(bytes));
        }

        void enqueue(const std::shared_ptr<request>& operation, std::string bytes) {
            for (std::size_t i = 0; i < operation->remaining; ++i)
            {
                slots.push_back(operation);
            }
            operation->sent = true;
            transport->async_send(
                std::move(bytes), [weak = this->weak_from_this()](arknet::error_code error, std::size_t) {
                    if (error)
                    {
                        if (auto self = weak.lock())
                        {
                            self->shutdown(client_errc::disconnected, error);
                        }
                    }
                });
        }

        void cancel_request(std::shared_ptr<request> operation, client_errc reason) {
            if (operation->completed || terminal)
            {
                return;
            }
            complete(operation, result{reason, {}, std::move(operation->replies), operation->sent});
            if (terminal || !operation->remaining)
            {
                return;
            }
            // Do not remove sent slots or free their admission budget: later replies must still be drained in order.
            const auto deadline =
                arknet::detail::checked_deadline(std::chrono::steady_clock::now(), options.drain_timeout);
            if (!deadline)
            {
                shutdown(client_errc::invalid_command);
                return;
            }
            operation->timer->expires_at(*deadline);
            operation->timer->async_wait(
                [weak = this->weak_from_this(), operation = std::weak_ptr(operation)](arknet::error_code error) {
                    if (!error)
                    {
                        if (auto self = weak.lock())
                        {
                            if (auto current = operation.lock(); current && current->remaining)
                            {
                                self->shutdown(client_errc::timeout);
                            }
                        }
                    }
                });
        }

        void received(std::string_view bytes) {
            if (terminal)
            {
                return;
            }
            if (!decoder.append(bytes))
            {
                shutdown(client_errc::protocol_error);
                return;
            }
            while (!terminal)
            {
                value reply;
                auto status = decoder.next(reply);
                if (status == detail::resp_status::need_more)
                {
                    return;
                }
                if (status != detail::resp_status::ready)
                {
                    shutdown(client_errc::protocol_error);
                    return;
                }
                if (reply.type == value_type::push)
                {
                    if (push)
                    {
                        push(std::move(reply));
                    }
                    continue;
                }
                if (slots.empty())
                {
                    shutdown(client_errc::protocol_error);
                    return;
                }
                auto operation = slots.front();
                slots.pop_front();
                --operation->remaining;
                if (!operation->completed)
                {
                    const auto maximum = options.max_pipeline_response_bytes;
                    auto size = operation->response_bytes;
                    if (!accumulate_response_size(reply, size, maximum))
                    {
                        // Keep this slot visible to shutdown so it also receives its failure.
                        slots.push_front(operation);
                        ++operation->remaining;
                        shutdown(client_errc::response_limit);
                        return;
                    }
                    operation->response_bytes = size;
                    operation->replies.push_back(std::move(reply));
                }
                if (!operation->remaining)
                {
                    release(operation);
                    cancel_timer(operation->timer);
                    if (operation->handshake)
                    {
                        finish_handshake(operation);
                    }
                    else
                    {
                        complete(operation, result{client_errc::none, {}, std::move(operation->replies)});
                    }
                }
            }
        }

        static bool accumulate_response_size(const value& reply, std::size_t& bytes, std::size_t maximum) {
            if (sizeof(value) > maximum - bytes)
            {
                return false;
            }
            bytes += sizeof(value);
            if (reply.text.size() > maximum - bytes)
            {
                return false;
            }
            bytes += reply.text.size();
            for (const auto* children : {&reply.elements, &reply.attributes})
            {
                for (const auto& child : *children)
                {
                    if (!accumulate_response_size(child, bytes, maximum))
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        void finish_handshake(const std::shared_ptr<request>& operation) {
            if (terminal || close_requested)
            {
                shutdown(client_errc::closed);
                return;
            }
            bool accepted = !operation->replies.empty();
            for (const auto& reply : operation->replies)
            {
                accepted = accepted && reply.type != value_type::error && reply.type != value_type::bulk_error;
            }
            if (connection.protocol == protocol_version::resp3)
            {
                const auto& hello = operation->replies.front();
                bool protocol_matches = false;
                if (hello.type == value_type::map)
                {
                    for (std::size_t i = 0; i + 1 < hello.elements.size(); i += 2)
                    {
                        protocol_matches = protocol_matches || (hello.elements[i].text == "proto" &&
                                                                   hello.elements[i + 1].type == value_type::integer &&
                                                                   hello.elements[i + 1].integer == 3);
                    }
                }
                accepted = accepted && protocol_matches;
                for (std::size_t i = 1; i < operation->replies.size(); ++i)
                {
                    accepted = accepted && operation->replies[i].type == value_type::simple_string &&
                               operation->replies[i].text == "OK";
                }
            }
            else
            {
                const auto ping = connection.database ? operation->replies.size() - 2 : operation->replies.size() - 1;
                accepted = accepted && operation->replies[ping].type == value_type::simple_string &&
                           operation->replies[ping].text == "PONG";
                for (std::size_t i = 0; i < operation->replies.size(); ++i)
                {
                    if (i != ping)
                    {
                        accepted = accepted && operation->replies[i].type == value_type::simple_string &&
                                   operation->replies[i].text == "OK";
                    }
                }
            }
            if (!accepted)
            {
                auto handler = std::exchange(connect_handler, {});
                auto replies = std::move(operation->replies);
                shutdown(client_errc::handshake_failed);
                if (handler)
                {
                    handler(result{client_errc::handshake_failed, {}, std::move(replies)});
                }
                return;
            }
            cancel_timer(connect_timer);
            ready = true;
            if (close_requested)
            {
                shutdown(client_errc::closed);
                return;
            }
            auto handler = std::exchange(connect_handler, {});
            if (handler)
            {
                handler(result{client_errc::none, {}, std::move(operation->replies)});
            }
        }

        // Only native cleanup is contained here. Protocol handlers are invoked
        // outside this boundary and retain their documented no-throw contract.
        void cleanup_transport() noexcept {
            native_cleanup_pending = true;
            try
            {
                arknet::error_code ignored;
                transport->socket().cancel(ignored);
                transport->socket().close(ignored);
                transport->stop();
                native_cleanup_pending = false;
                native_cleanup_failure = {};
            }
            catch (...)
            { native_cleanup_failure = std::current_exception(); }
        }

        void shutdown(client_errc reason, arknet::error_code error = {}) {
            if (terminal)
            {
                return;
            }
            terminal = true;
            ready = false;
            cancel_timer(connect_timer);
            auto pending = std::move(slots);
            slots.clear();
            cleanup_transport();
            auto connecting = std::exchange(connect_handler, {});
            if (connecting)
            {
                connecting(result{reason, error});
            }
            for (const auto& operation : pending)
            {
                release(operation);
                cancel_timer(operation->timer);
                operation->remaining = 0;
                complete(operation, result{reason, error, std::move(operation->replies), operation->sent});
            }
        }

        void close_on_lane() {
            close_requested = true;
            close_started = true;
            shutdown(client_errc::closed);
        }

        // An explicit permanent-close fence makes one further attempt after a
        // failed native stop. Successful submission is followed by runtime join.
        std::exception_ptr reconcile_close_on_lane() {
            close_on_lane();
            if (native_cleanup_pending)
            {
                cleanup_transport();
            }
            return native_cleanup_failure;
        }

        void request_close() {
            if (!close_requested.exchange(true))
            {
                try
                {
                    asio::dispatch(executor(), [self = this->shared_from_this()] { self->close_on_lane(); });
                }
                catch (...)
                {
                    // A failed allocation before actor entry did not close the
                    // transport. Permit a later ordinary close() to retry.
                    // Inline actor/user code may already have begun shutdown;
                    // never roll back that externally visible transition.
                    if (!close_started.load())
                    {
                        close_requested = false;
                    }
                    throw;
                }
            }
        }

        // Called only after the owned or borrowing pool runtime has joined.
        // Keep its context alive until every socket/TLS/timer has retired.
        void retire_context_resources() {
            slots.clear();
            connect_handler = {};
            connect_timer.reset();
            push = {};
            decoder = detail::resp_decoder{};
            connection = {};
            options = {};
            transport.reset();
            std::lock_guard lock(publishing_mutex);
            runtime.reset();
        }

        bool close_and_wait() {
            {
                std::unique_lock lock(publishing_mutex);
                if (!owns_runtime || (runtime && runtime->running_in_threads()) ||
                    publication::active_on_this_thread(*this) || finalizer == std::this_thread::get_id())
                {
                    arknet::set_last_error(asio::error::operation_not_supported);
                    return false;
                }
                if (terminating)
                {
                    publishing_changed.wait(lock, [&] { return terminated || shutdown_failure; });
                    if (shutdown_failure)
                    {
                        std::rethrow_exception(shutdown_failure);
                    }
                    arknet::clear_last_error();
                    return true;
                }
                terminating = true;
                finalizer = std::this_thread::get_id();
                publishing_changed.wait(lock, [&] { return publishers == 0; });
            }
            try
            {
                // The permanent gate has sealed every API publication. Reconcile
                // failed native cleanup on its lane before joining the runtime.
                auto completion = std::make_shared<std::promise<void>>();
                auto ready = completion->get_future();
                const auto self = this->shared_from_this();
                asio::post(executor(), [self, completion] {
                    try
                    {
                        if (auto failure = self->reconcile_close_on_lane())
                        {
                            completion->set_exception(std::move(failure));
                        }
                        else
                        {
                            completion->set_value();
                        }
                    }
                    catch (...)
                    { completion->set_exception(std::current_exception()); }
                });
                ready.get();
                auto scheduler = runtime;
                scheduler->stop();
                // A late connect callback can attempt native cleanup after the
                // fence. The joined lane makes its final status safe to inspect.
                if (native_cleanup_pending)
                {
                    std::rethrow_exception(native_cleanup_failure);
                }

                // Only the finalizer owns these resources after worker join.
                // Keep the context alive until timers, sockets and TLS objects
                // and all retained user callables have actually been destroyed.
                retire_context_resources();
                scheduler.reset();
                {
                    std::lock_guard lock(publishing_mutex);
                    terminated = true;
                    finalizer = {};
                }
                publishing_changed.notify_all();
                arknet::clear_last_error();
                return true;
            }
            catch (...)
            {
                {
                    std::lock_guard lock(publishing_mutex);
                    shutdown_failure = std::current_exception();
                    finalizer = {};
                }
                publishing_changed.notify_all();
                throw;
            }
        }
    };

    static request_id reject_terminated(completion_handler handler) {
        if (handler)
        {
            handler(result{client_errc::closed});
        }
        return 0;
    }

public:
    explicit basic_client(client_options options = {}, std::string certificate_authority = {})
        : state_(std::make_shared<state>(options, certificate_authority)) {
        state_->initialize();
    }

    basic_client(const basic_client&) = delete;
    basic_client& operator=(const basic_client&) = delete;

    ~basic_client() {
        close();
    }

    void async_connect(connect_options options, completion_handler handler) {
        auto self = state_;
        typename state::publication publishing(*self);
        if (!publishing)
        {
            reject_terminated(std::move(handler));
            return;
        }
        const auto deadline =
            arknet::detail::checked_deadline(std::chrono::steady_clock::now(), self->options.connect_timeout);
        asio::post(
            self->executor(), [self, options = std::move(options), handler = std::move(handler), deadline]() mutable {
                if (!deadline)
                {
                    if (handler)
                    {
                        handler(result{client_errc::invalid_command});
                    }
                }
                else
                {
                    self->connect(std::move(options), std::move(handler), *deadline);
                }
            });
    }

    request_id async_execute(command arguments, completion_handler handler,
        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
        auto self = state_;
        typename state::publication publishing(*self);
        if (!publishing)
        {
            return reject_terminated(std::move(handler));
        }
        std::vector<command> commands;
        commands.push_back(std::move(arguments));
        return async_pipeline(std::move(commands), std::move(handler), timeout);
    }

    request_id async_pipeline(std::vector<command> commands, completion_handler handler,
        std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
        auto self = state_;
        typename state::publication publishing(*self);
        if (!publishing)
        {
            return reject_terminated(std::move(handler));
        }
        auto operation = std::make_shared<request>();
        operation->id = self->next_id.fetch_add(1, std::memory_order_relaxed);
        operation->commands = operation->remaining = commands.size();
        operation->handler = std::move(handler);
        const auto deadline = arknet::detail::checked_deadline(std::chrono::steady_clock::now(),
            timeout == std::chrono::milliseconds::zero() ? self->options.command_timeout : timeout);
        std::string bytes;
        client_errc error = deadline ? client_errc::none : client_errc::invalid_command;
        try
        {
            if (deadline)
            {
                bytes = state::encode(commands, self->options.max_pending_bytes);
                operation->wire_bytes = bytes.size();
                if (!self->reserve(operation))
                {
                    error = client_errc::queue_full;
                }
            }
        }
        catch (const std::length_error&)
        { error = client_errc::queue_full; }
        catch (const std::invalid_argument&)
        { error = client_errc::invalid_command; }
        catch (const std::domain_error&)
        { error = client_errc::unsupported_command; }
        if (error != client_errc::none)
        {
            // A rejected submission must not retain its large payload in the IO callback queue.
            std::string{}.swap(bytes);
        }
        try
        {
            asio::post(self->executor(), [self, operation, bytes = std::move(bytes), deadline, error]() mutable {
                if (error != client_errc::none)
                {
                    self->complete(operation, result{error});
                }
                else
                {
                    self->submit(operation, std::move(bytes), *deadline);
                }
            });
        }
        catch (...)
        {
            self->release(operation);
            throw;
        }
        return operation->id;
    }

    // Completes the caller without retracting a transport-queued write or replaying it.
    void cancel(request_id id) {
        auto self = state_;
        typename state::publication publishing(*self);
        if (!publishing)
        {
            return;
        }
        asio::post(self->executor(), [self, id] {
            for (const auto& operation : self->slots)
            {
                if (!operation->handshake && operation->id == id)
                {
                    self->cancel_request(operation, client_errc::cancelled);
                    break;
                }
            }
        });
    }

    void set_push_handler(push_handler handler) {
        auto self = state_;
        typename state::publication publishing(*self);
        if (!publishing)
        {
            return;
        }
        asio::post(
            self->executor(), [self, handler = std::move(handler)]() mutable { self->push = std::move(handler); });
    }

    bool is_connected() const noexcept {
        return state_->ready.load() && !state_->close_requested.load();
    }

    void close() {
        auto self = state_;
        typename state::publication publishing(*self);
        if (publishing)
        {
            self->request_close();
        }
    }

    // Permanently closes an independent client, retires accepted callbacks and
    // captures, joins its owned IO worker and destroys transport/TLS resources.
    // Returns false with operation_not_supported on its own IO worker, during
    // reentrant publication/finalization or for a pool-owned child; no permanent
    // transition is made in those cases. Never hold locks needed by callbacks.
    // After the gate closes, new commands/connects reject synchronously with
    // closed and ID 0; those later handlers may propagate exceptions. Stop and
    // wait for application callers separately. This is not a pool, process-wide
    // cleanup or dynamic-library unload barrier.
    bool close_and_wait() {
        auto self = state_;
        return self->close_and_wait();
    }

private:
    friend class basic_pool<Transport>;

    basic_client(
        client_options options, std::string certificate_authority, std::shared_ptr<arknet::detail::io_pool> runtime)
        : state_(std::make_shared<state>(options, certificate_authority, std::move(runtime))) {
        state_->initialize();
    }

    // Pool-only barrier: RESET follows all prior slots, including cancelled sent commands.
    // Its deadline covers the old replies and re-establishing protocol/authentication/database.
    void async_reset(completion_handler handler, std::chrono::milliseconds timeout) {
        auto self = state_;
        typename state::publication publishing(*self);
        if (!publishing)
        {
            reject_terminated(std::move(handler));
            return;
        }
        const auto deadline = arknet::detail::checked_deadline(std::chrono::steady_clock::now(), timeout);
        asio::post(self->executor(), [self, handler = std::move(handler), deadline]() mutable {
            if (!deadline)
            {
                if (handler)
                {
                    handler(result{client_errc::invalid_command});
                }
                return;
            }
            if (self->terminal || !self->ready)
            {
                handler(result{client_errc::closed});
                return;
            }
            std::string bytes;
            auto encoding_error = client_errc::none;
            try
            { bytes = state::encode({{"RESET"}}, self->options.max_pending_bytes); }
            catch (const std::length_error&)
            { encoding_error = client_errc::queue_full; }
            catch (const std::bad_alloc&)
            { encoding_error = client_errc::queue_full; }
            if (encoding_error != client_errc::none)
            {
                self->shutdown(encoding_error);
                if (handler)
                {
                    handler(result{encoding_error});
                }
                return;
            }
            self->ready = false;
            self->connect_handler = std::move(handler);
            self->connect_timer = std::make_shared<asio::steady_timer>(self->executor());
            self->connect_timer->expires_at(*deadline);
            self->connect_timer->async_wait([weak = std::weak_ptr(self)](arknet::error_code error) {
                if (!error)
                {
                    if (auto current = weak.lock())
                    {
                        current->shutdown(client_errc::timeout);
                    }
                }
            });
            auto operation = std::make_shared<request>();
            operation->commands = operation->remaining = 1;
            operation->handler = [weak = std::weak_ptr(self)](result response) {
                if (auto current = weak.lock(); current && !current->terminal)
                {
                    if (!response || response.replies.size() != 1 ||
                        response.replies.front().type != value_type::simple_string ||
                        response.replies.front().text != "RESET")
                    {
                        current->shutdown(client_errc::handshake_failed);
                    }
                    else
                    {
                        current->connected({});
                    }
                }
            };
            self->enqueue(operation, std::move(bytes));
        });
    }

    std::shared_ptr<state> state_;
};

using client = basic_client<arknet::tcp_client>;
#if defined(ARKNET_ENABLE_SSL)
// The existing TLS transport verifies the trust chain and the connect host/IP by default.
using tls_client = basic_client<arknet::tcps_client>;
#endif

} // namespace arknet::redis
