// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arknet/base/detail/deadline.hpp>
#include <arknet/mysql/connection.hpp>

namespace arknet::mysql {

struct pool_options
{
    std::size_t max_connections = 8;
    std::size_t max_waiters = 1024;
    std::size_t io_threads = 1;
    std::chrono::milliseconds acquire_timeout{5000};
    // Starts when release is requested, including pending callback drain.
    std::chrono::milliseconds reset_timeout{5000};
    connect_options connection;
};

// Each lease exclusively owns a connection until all its submitted callbacks
// have returned and COM_RESET_CONNECTION succeeds. Failed resets discard the
// connection. The configured initial database is restored with a quoted USE;
// if no initial database is configured, returned connections are discarded.
// No statement or transaction is replayed on another connection.
class pool
{
    struct state;
    struct waiter;
    struct lease_control;

    enum class entry_phase
    {
        connecting,
        idle,
        leased,
        returning,
        resetting
    };

    struct entry
    {
        std::shared_ptr<mysql::connection> client;
        entry_phase phase = entry_phase::connecting;
        std::weak_ptr<waiter> waiting;
        std::shared_ptr<asio::steady_timer> return_deadline;
        std::uint64_t return_generation = 0;
    };

    struct lease_control
    {
        std::shared_ptr<entry> session;
        std::mutex mutex;
        std::size_t pending = 0;
        bool released = false;
        bool unhealthy = false;
        std::chrono::milliseconds reset_timeout;
        std::chrono::steady_clock::time_point return_time;
        std::function<bool()> available;
        std::function<void(std::shared_ptr<entry>, std::chrono::steady_clock::time_point)> begin_return;
        std::function<void(std::shared_ptr<entry>, bool, std::chrono::steady_clock::time_point)> drained;

        void finish(const connection_result& result) {
            std::function<void(std::shared_ptr<entry>, bool, std::chrono::steady_clock::time_point)> returning;
            bool discard = false;
            std::chrono::steady_clock::time_point deadline;
            {
                std::lock_guard lock(mutex);
                unhealthy = unhealthy || result.outcome_unknown || !session->client->is_open();
                --pending;
                if (released && !pending)
                {
                    returning = std::exchange(drained, {});
                    discard = unhealthy;
                    deadline = return_time;
                }
            }
            if (returning)
            {
                returning(session, discard, deadline);
            }
        }

        void release(bool discard = false) {
            std::function<void(std::shared_ptr<entry>, std::chrono::steady_clock::time_point)> starting;
            std::function<void(std::shared_ptr<entry>, bool, std::chrono::steady_clock::time_point)> returning;
            std::chrono::steady_clock::time_point deadline;
            {
                std::lock_guard lock(mutex);
                if (released)
                {
                    return;
                }
                released = true;
                unhealthy = unhealthy || discard;
                const auto now = std::chrono::steady_clock::now();
                const auto expires = arknet::detail::checked_deadline(now, reset_timeout);
                unhealthy = unhealthy || !expires;
                return_time = expires.value_or(now);
                deadline = return_time;
                starting = std::exchange(begin_return, {});
                if (!pending)
                {
                    returning = std::exchange(drained, {});
                }
            }
            if (starting)
            {
                starting(session, deadline);
            }
            if (returning)
            {
                returning(session, unhealthy, deadline);
            }
        }
    };

public:
    class lease
    {
        friend class pool;
        std::shared_ptr<lease_control> control_;

        explicit lease(std::shared_ptr<lease_control> control)
            : control_(std::move(control)) {}

        template<class Result, class Handler, class Submit>
        request_id submit(Handler handler, Submit submitter) {
            auto control = control_;
            if (!control)
            {
                if (handler)
                {
                    Result result;
                    if constexpr (std::is_same_v<Result, connection_result>)
                    {
                        result.code = connection_errc::not_connected;
                        result.message = "Empty MySQL pool lease";
                    }
                    else
                    {
                        result.status.code = connection_errc::not_connected;
                        result.status.message = "Empty MySQL pool lease";
                    }
                    handler(std::move(result));
                }
                return 0;
            }
            std::unique_lock lock(control->mutex);
            if (control->released || !control->available())
            {
                const auto id = control->session->client->state_->next_request.fetch_add(1);
                asio::post(control->session->client->state_->executor, [handler = std::move(handler)]() mutable {
                    if (handler)
                    {
                        Result result;
                        if constexpr (std::is_same_v<Result, connection_result>)
                        {
                            result.code = connection_errc::not_connected;
                            result.message = "MySQL pool lease released or closed";
                        }
                        else
                        {
                            result.status.code = connection_errc::not_connected;
                            result.status.message = "MySQL pool lease released or closed";
                        }
                        handler(std::move(result));
                    }
                });
                return id;
            }
            ++control->pending;
            try
            {
                return submitter(
                    *control->session->client, [control, handler = std::move(handler)](Result response) mutable {
                        connection_result status;
                        if constexpr (std::is_same_v<Result, connection_result>)
                        {
                            status = response;
                        }
                        else
                        {
                            status = response.status;
                        }
                        if (handler)
                        {
                            handler(std::move(response));
                        }
                        control->finish(status);
                    });
            }
            catch (...)
            {
                --control->pending;
                throw;
            }
        }

    public:
        lease() = default;
        lease(const lease&) = delete;
        lease& operator=(const lease&) = delete;
        lease(lease&& other) noexcept = default;

        lease& operator=(lease&& other) noexcept {
            if (this != &other)
            {
                release();
                control_ = std::move(other.control_);
            }
            return *this;
        }

        ~lease() {
            release();
        }

        explicit operator bool() const noexcept {
            if (!control_)
            {
                return false;
            }
            std::lock_guard lock(control_->mutex);
            return !control_->released && control_->available() && control_->session->client->is_open();
        }

        request_id async_query(std::string sql, connection::query_handler handler, query_options options = {}) {
            return submit<query_result>(std::move(handler),
                [sql = std::move(sql), options](mysql::connection& client, auto completion) mutable {
                    return client.async_query(std::move(sql), std::move(completion), options);
                });
        }

        request_id async_prepare(std::string sql, connection::prepare_handler handler, query_options options = {}) {
            return submit<prepare_result>(std::move(handler),
                [sql = std::move(sql), options](mysql::connection& client, auto completion) mutable {
                    return client.async_prepare(std::move(sql), std::move(completion), options);
                });
        }

        request_id async_execute(const prepared_statement& statement, std::vector<field_value> parameters,
            connection::query_handler handler, query_options options = {}) {
            return submit<query_result>(std::move(handler), [statement, parameters = std::move(parameters), options](
                                                                mysql::connection& client, auto completion) mutable {
                return client.async_execute(statement, std::move(parameters), std::move(completion), options);
            });
        }

        request_id async_close(
            const prepared_statement& statement, connection::result_handler handler = {}, query_options options = {}) {
            return submit<connection_result>(
                std::move(handler), [statement, options](mysql::connection& client, auto completion) mutable {
                    return client.async_close(statement, std::move(completion), options);
                });
        }

        request_id async_begin(connection::query_handler handler, query_options options = {}) {
            return async_query("START TRANSACTION", std::move(handler), options);
        }

        request_id async_commit(connection::query_handler handler, query_options options = {}) {
            return async_query("COMMIT", std::move(handler), options);
        }

        request_id async_rollback(connection::query_handler handler, query_options options = {}) {
            return async_query("ROLLBACK", std::move(handler), options);
        }

        void cancel(request_id id) {
            if (control_)
            {
                std::lock_guard lock(control_->mutex);
                if (!control_->released && control_->available())
                {
                    control_->session->client->cancel(id);
                }
            }
        }

        void release() {
            if (control_)
            {
                control_->release();
            }
        }

        void discard() {
            if (control_)
            {
                control_->release(true);
            }
        }
    };

    struct acquire_result
    {
        connection_result status;
        lease connection;

        explicit operator bool() const noexcept {
            return bool(status) && bool(connection);
        }
    };

    using acquire_handler = std::function<void(acquire_result)>;

private:
    struct waiter
    {
        request_id id = 0;
        acquire_handler handler;
        std::shared_ptr<asio::steady_timer> timer;
        std::weak_ptr<entry> assigned;
        std::chrono::steady_clock::time_point deadline;
        bool admitted = false;
        bool completed = false;
    };

    struct state : std::enable_shared_from_this<state>
    {
        pool_options options;
        std::shared_ptr<arknet::detail::io_pool> runtime;
        asio::any_io_executor executor;
        std::deque<std::shared_ptr<waiter>> queue;
        std::unordered_map<request_id, std::shared_ptr<waiter>> pending;
        std::vector<std::shared_ptr<entry>> entries;
        std::atomic<request_id> next_id{1};
        std::atomic<std::size_t> admitted{};
        std::atomic<bool> close_requested{};
        bool closed = false;

        explicit state(pool_options configuration)
            : options(std::move(configuration)) {
            const auto now = std::chrono::steady_clock::now();
            if (!options.max_connections || !options.max_waiters || !options.io_threads ||
                !arknet::detail::checked_deadline(now, options.acquire_timeout) ||
                !arknet::detail::checked_deadline(now, options.reset_timeout) ||
                !arknet::detail::checked_deadline(now, options.connection.timeout))
            {
                throw std::invalid_argument("MySQL pool limits and deadlines must be positive and representable");
            }
            runtime = std::shared_ptr<arknet::detail::io_pool>(
                new arknet::detail::io_pool(options.io_threads), arknet::detail::iopool_deleter{});
            executor = runtime->get(0)->executor();
            if (!runtime->start())
            {
                throw std::runtime_error("Cannot start MySQL pool IO runtime");
            }
        }

        ~state() {
            OPENSSL_cleanse(options.connection.password.data(), options.connection.password.size());
        }

        static connection_result failure(connection_errc code, std::string message, error_code error = {}) {
            return {code, error, 0, {}, std::move(message)};
        }

        bool reserve() {
            auto count = admitted.load();
            while (count < options.max_waiters)
            {
                if (admitted.compare_exchange_weak(count, count + 1))
                {
                    return true;
                }
            }
            return false;
        }

        bool contains(const std::shared_ptr<entry>& session) const {
            return std::find(entries.begin(), entries.end(), session) != entries.end();
        }

        void discard(std::shared_ptr<entry> session) {
            std::erase(entries, session);
            if (session->return_deadline)
            {
                session->return_deadline->cancel();
            }
            session->client->close();
        }

        void complete(std::shared_ptr<waiter> request, acquire_result response) {
            if (request->completed)
            {
                return;
            }
            request->completed = true;
            if (request->timer)
            {
                request->timer->cancel();
            }
            pending.erase(request->id);
            std::erase(queue, request);
            if (std::exchange(request->admitted, false))
            {
                --admitted;
            }
            auto handler = std::move(request->handler);
            if (handler)
            {
                handler(std::move(response));
            }
        }

        lease make_lease(const std::shared_ptr<entry>& session) {
            session->phase = entry_phase::leased;
            auto control = std::make_shared<lease_control>();
            control->session = session;
            control->reset_timeout = options.reset_timeout;
            control->available = [weak = weak_from_this()] {
                const auto self = weak.lock();
                return self && !self->close_requested.load();
            };
            control->begin_return = [weak = weak_from_this()](std::shared_ptr<entry> current, auto deadline) {
                if (auto self = weak.lock())
                {
                    asio::post(self->executor, [self, current, deadline] { self->begin_return(current, deadline); });
                }
                else
                {
                    current->client->close();
                }
            };
            control->drained = [weak = weak_from_this()](
                                   std::shared_ptr<entry> current, bool unhealthy, auto deadline) {
                if (auto self = weak.lock())
                {
                    asio::post(self->executor,
                        [self, current, unhealthy, deadline] { self->returned(current, unhealthy, deadline); });
                }
                else
                {
                    current->client->close();
                }
            };
            return lease(std::move(control));
        }

        void acquire(const std::shared_ptr<waiter>& request) {
            if (closed || close_requested)
            {
                complete(request, {failure(connection_errc::cancelled, "MySQL pool closed"), {}});
                return;
            }
            if (!request->admitted)
            {
                complete(request, {failure(connection_errc::queue_full, "MySQL pool waiter limit exceeded"), {}});
                return;
            }
            if (std::chrono::steady_clock::now() >= request->deadline)
            {
                complete(request,
                    {failure(connection_errc::timeout, "MySQL pool acquire deadline exceeded", asio::error::timed_out),
                        {}});
                return;
            }
            request->timer = std::make_shared<asio::steady_timer>(executor);
            request->timer->expires_at(request->deadline);
            request->timer->async_wait([weak = weak_from_this(), waiting = std::weak_ptr(request)](error_code error) {
                if (!error)
                {
                    if (auto self = weak.lock())
                    {
                        if (auto current = waiting.lock())
                        {
                            self->cancel(current, connection_errc::timeout);
                        }
                    }
                }
            });
            pending.emplace(request->id, request);
            queue.push_back(request);
            pump();
        }

        void cancel(std::shared_ptr<waiter> request, connection_errc reason) {
            if (request->completed)
            {
                return;
            }
            if (auto assigned = request->assigned.lock(); assigned && assigned->phase == entry_phase::connecting)
            {
                if (assigned->waiting.lock() == request)
                {
                    discard(assigned);
                }
            }
            complete(request, {failure(reason,
                                   reason == connection_errc::timeout ? "MySQL pool acquire deadline exceeded"
                                                                      : "MySQL pool acquire cancelled",
                                   reason == connection_errc::timeout ? error_code(asio::error::timed_out)
                                                                      : error_code(asio::error::operation_aborted)),
                                  {}});
            pump();
        }

        void pump() {
            while (!closed && !close_requested && !queue.empty())
            {
                if (std::chrono::steady_clock::now() >= queue.front()->deadline)
                {
                    auto request = queue.front();
                    if (auto assigned = request->assigned.lock();
                        assigned && assigned->phase == entry_phase::connecting && assigned->waiting.lock() == request)
                    {
                        discard(assigned);
                    }
                    complete(request, {failure(connection_errc::timeout, "MySQL pool acquire deadline exceeded",
                                           asio::error::timed_out),
                                          {}});
                    continue;
                }
                const auto idle = std::find_if(entries.begin(), entries.end(),
                    [](const auto& session) { return session->phase == entry_phase::idle; });
                if (idle != entries.end())
                {
                    auto session = *idle;
                    if (!session->client->is_open())
                    {
                        discard(session);
                        continue;
                    }
                    auto request = queue.front();
                    complete(request, {{}, make_lease(session)});
                    continue;
                }
                if (entries.size() >= options.max_connections)
                {
                    return;
                }
                auto waiting = std::find_if(queue.begin(), queue.end(), [](const auto& request) {
                    const auto assigned = request->assigned.lock();
                    return !assigned || assigned->phase != entry_phase::connecting;
                });
                if (waiting == queue.end())
                {
                    return;
                }
                auto request = *waiting;
                auto session = std::make_shared<entry>();
                session->client = std::shared_ptr<mysql::connection>(new mysql::connection(runtime));
                session->waiting = request;
                request->assigned = session;
                entries.push_back(session);
                session->client->async_connect(
                    options.connection, [weak = weak_from_this(), session](connection_result result) mutable {
                        if (auto self = weak.lock())
                        {
                            asio::post(self->executor, [self, session, result = std::move(result)]() mutable {
                                if (!self->contains(session))
                                {
                                    return;
                                }
                                if (!result || self->closed || self->close_requested)
                                {
                                    auto request = session->waiting.lock();
                                    self->discard(session);
                                    if (request && !request->completed)
                                    {
                                        self->complete(
                                            request, {self->closed || self->close_requested
                                                             ? failure(connection_errc::cancelled, "MySQL pool closed")
                                                             : std::move(result),
                                                         {}});
                                    }
                                }
                                else
                                {
                                    session->phase = entry_phase::idle;
                                }
                                self->pump();
                            });
                        }
                    });
            }
        }

        void begin_return(const std::shared_ptr<entry>& session, std::chrono::steady_clock::time_point deadline) {
            if (!contains(session))
            {
                session->client->close();
                return;
            }
            if (session->phase != entry_phase::leased)
            {
                return;
            }
            if (closed || close_requested || std::chrono::steady_clock::now() >= deadline)
            {
                discard(session);
                pump();
                return;
            }
            session->phase = entry_phase::returning;
            const auto return_generation = ++session->return_generation;
            session->return_deadline = std::make_shared<asio::steady_timer>(executor);
            session->return_deadline->expires_at(deadline);
            session->return_deadline->async_wait(
                [weak = weak_from_this(), current = std::weak_ptr(session), return_generation](error_code error) {
                    if (!error)
                    {
                        if (auto self = weak.lock())
                        {
                            if (auto session = current.lock();
                                session && self->contains(session) && session->return_generation == return_generation &&
                                (session->phase == entry_phase::returning || session->phase == entry_phase::resetting))
                            {
                                self->discard(session);
                                self->pump();
                            }
                        }
                    }
                });
        }

        void returned(
            const std::shared_ptr<entry>& session, bool unhealthy, std::chrono::steady_clock::time_point deadline) {
            if (!contains(session))
            {
                session->client->close();
                return;
            }
            if (closed || close_requested || unhealthy || !session->client->is_open() ||
                options.connection.database.empty() || std::chrono::steady_clock::now() >= deadline)
            {
                discard(session);
                pump();
                return;
            }
            if (session->phase == entry_phase::leased)
            {
                begin_return(session, deadline);
            }
            if (!contains(session) || session->phase != entry_phase::returning)
            {
                return;
            }
            session->phase = entry_phase::resetting;
            query_options reset_options;
            reset_options.timeout = (std::max)(std::chrono::milliseconds(1),
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
            session->client->async_reset(
                [weak = weak_from_this(), session](connection_result result) mutable {
                    if (auto self = weak.lock())
                    {
                        asio::post(self->executor, [self, session, result = std::move(result)] {
                            if (!self->contains(session))
                            {
                                return;
                            }
                            if (!result || self->closed || self->close_requested)
                            {
                                self->discard(session);
                                self->pump();
                            }
                            else
                            {
                                self->restore_database(session);
                            }
                        });
                    }
                },
                reset_options);
        }

        void restore_database(const std::shared_ptr<entry>& session) {
            const auto deadline = session->return_deadline->expiry();
            if (closed || close_requested || std::chrono::steady_clock::now() >= deadline)
            {
                discard(session);
                pump();
                return;
            }
            std::string sql = "USE `";
            for (const char character : options.connection.database)
            {
                sql.push_back(character);
                if (character == '`')
                {
                    sql.push_back('`');
                }
            }
            sql.push_back('`');
            query_options query;
            query.timeout = (std::max)(std::chrono::milliseconds(1),
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
            session->client->async_query(
                std::move(sql),
                [weak = weak_from_this(), session](query_result result) mutable {
                    if (auto self = weak.lock())
                    {
                        asio::post(self->executor, [self, session, result = std::move(result)] {
                            if (!self->contains(session))
                            {
                                return;
                            }
                            if (!result || self->closed || self->close_requested ||
                                std::chrono::steady_clock::now() >= session->return_deadline->expiry())
                            {
                                self->discard(session);
                            }
                            else
                            {
                                session->return_deadline->cancel();
                                session->phase = entry_phase::idle;
                            }
                            self->pump();
                        });
                    }
                },
                query);
        }

        void shutdown() {
            if (std::exchange(closed, true))
            {
                return;
            }
            auto waiting = std::move(pending);
            pending.clear();
            queue.clear();
            auto sessions = std::move(entries);
            entries.clear();
            for (auto& [id, request] : waiting)
            {
                complete(request, {failure(connection_errc::cancelled, "MySQL pool closed"), {}});
            }
            for (auto& session : sessions)
            {
                if (session->return_deadline)
                {
                    session->return_deadline->cancel();
                }
                session->client->close();
            }
        }
    };

    std::shared_ptr<state> state_;

public:
    explicit pool(pool_options options = {})
        : state_(std::make_shared<state>(std::move(options))) {}

    pool(const pool&) = delete;
    pool& operator=(const pool&) = delete;

    ~pool() {
        close();
    }

    request_id async_acquire(
        acquire_handler handler, std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
        auto self = state_;
        auto request = std::make_shared<waiter>();
        request->id = self->next_id.fetch_add(1);
        request->handler = std::move(handler);
        const auto expires = arknet::detail::checked_deadline(std::chrono::steady_clock::now(),
            timeout == std::chrono::milliseconds::zero() ? self->options.acquire_timeout : timeout);
        request->admitted = expires && self->reserve();
        request->deadline = expires.value_or(std::chrono::steady_clock::time_point{});
        try
        {
            asio::post(self->executor, [self, request, valid = bool(expires)] {
                if (!valid)
                {
                    self->complete(request,
                        {state::failure(connection_errc::invalid_options, "Invalid MySQL pool acquire deadline"), {}});
                }
                else
                {
                    self->acquire(request);
                }
            });
        }
        catch (...)
        {
            if (request->admitted)
            {
                --self->admitted;
            }
            throw;
        }
        return request->id;
    }

    void cancel(request_id id) {
        auto self = state_;
        asio::post(self->executor, [self, id] {
            const auto found = self->pending.find(id);
            if (found != self->pending.end())
            {
                self->cancel(found->second, connection_errc::cancelled);
            }
        });
    }

    void close() {
        auto self = state_;
        if (!self->close_requested.exchange(true))
        {
            asio::post(self->executor, [self] { self->shutdown(); });
        }
    }
};

} // namespace arknet::mysql
