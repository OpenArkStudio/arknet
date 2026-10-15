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
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <arknet/redis/client.hpp>

namespace arknet::redis {

struct pool_options
{
    std::size_t max_connections{8};
    std::size_t max_waiters{1024};
    std::chrono::milliseconds acquire_timeout{5000};
    std::chrono::milliseconds reset_timeout{5000};
    connect_options connection;
    client_options client_configuration;
    std::string certificate_authority;
};

// Exclusive leases preserve transactions and connection state. Returning a lease runs RESET,
// then restores the configured authentication/protocol/database before another borrower can use it.
// Servers without RESET can still be used: their connections are discarded on return.
template<class Transport>
class basic_pool
{
    using client_type = basic_client<Transport>;
    struct waiter;

    enum class entry_phase
    {
        connecting,
        idle,
        leased,
        resetting
    };

    struct entry
    {
        std::shared_ptr<client_type> client;
        entry_phase phase{entry_phase::connecting};
        std::weak_ptr<waiter> waiting;
    };

    struct lease_control
    {
        std::shared_ptr<entry> connection;
        std::function<void(std::shared_ptr<entry>, bool)> return_connection;
        std::mutex mutex;
        std::size_t pending{};
        bool released{};
        bool discard{};

        void finish() {
            std::function<void(std::shared_ptr<entry>, bool)> returning;
            bool unhealthy{};
            {
                std::lock_guard lock(mutex);
                --pending;
                if (released && !pending)
                {
                    returning = std::exchange(return_connection, {});
                    unhealthy = discard;
                }
            }
            if (returning)
            {
                returning(connection, unhealthy);
            }
        }

        void release(bool unhealthy = false) {
            std::function<void(std::shared_ptr<entry>, bool)> returning;
            bool discard_connection{};
            {
                std::lock_guard lock(mutex);
                released = true;
                discard = discard || unhealthy;
                if (!pending)
                {
                    returning = std::exchange(return_connection, {});
                    discard_connection = discard;
                }
            }
            if (returning)
            {
                returning(connection, discard_connection);
            }
        }
    };

public:
    class lease
    {
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
            return !control_->released && control_->connection->client->is_connected();
        }

        request_id async_execute(command arguments, typename client_type::completion_handler handler,
            std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
            std::vector<command> commands;
            commands.push_back(std::move(arguments));
            return async_pipeline(std::move(commands), std::move(handler), timeout);
        }

        request_id async_pipeline(std::vector<command> commands, typename client_type::completion_handler handler,
            std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
            auto control = control_;
            if (!control)
            {
                // An empty/moved-from lease has no executor. It rejects synchronously without a submission.
                if (handler)
                {
                    handler(result{client_errc::closed});
                }
                return 0;
            }
            std::unique_lock lock(control->mutex);
            if (control->released)
            {
                auto client = control->connection->client;
                auto id = client->state_->next_id.fetch_add(1);
                lock.unlock();
                asio::post(client->state_->executor(), [handler = std::move(handler)]() mutable {
                    if (handler)
                    {
                        handler(result{client_errc::closed});
                    }
                });
                return id;
            }
            ++control->pending;
            try
            {
                return control->connection->client->async_pipeline(
                    std::move(commands),
                    [control, handler = std::move(handler)](result response) mutable {
                        {
                            std::lock_guard callback_lock(control->mutex);
                            control->discard = control->discard || response.error == client_errc::disconnected ||
                                               response.error == client_errc::protocol_error ||
                                               response.error == client_errc::response_limit ||
                                               response.error == client_errc::closed;
                        }
                        if (handler)
                        {
                            handler(std::move(response));
                        }
                        control->finish();
                    },
                    timeout);
            }
            catch (...)
            {
                --control->pending;
                throw;
            }
        }

        void cancel(request_id id) {
            if (control_)
            {
                std::lock_guard lock(control_->mutex);
                if (!control_->released)
                {
                    control_->connection->client->cancel(id);
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

    private:
        friend class basic_pool;

        explicit lease(std::shared_ptr<lease_control> control)
            : control_(std::move(control)) {}

        std::shared_ptr<lease_control> control_;
    };

    struct acquire_result
    {
        client_errc error{client_errc::none};
        arknet::error_code transport_error;
        lease connection;
        std::vector<value> replies;

        explicit operator bool() const noexcept {
            return error == client_errc::none && bool(connection);
        }
    };

    using completion_handler = std::function<void(acquire_result)>;

private:
    struct waiter
    {
        request_id id{};
        completion_handler handler;
        std::shared_ptr<asio::steady_timer> timer;
        std::weak_ptr<entry> assigned;
        bool completed{};
        bool admitted{};
    };

    struct state : std::enable_shared_from_this<state>
    {
        pool_options options;
        std::shared_ptr<arknet::detail::io_pool> runtime;
        std::deque<std::shared_ptr<waiter>> queue;
        std::unordered_map<request_id, std::shared_ptr<waiter>> pending;
        std::vector<std::shared_ptr<entry>> entries;
        std::atomic<request_id> next_id{1};
        std::atomic_size_t admitted{};
        std::atomic_bool close_requested{};
        bool closed{};

        explicit state(pool_options configuration)
            : options(std::move(configuration))
            , runtime(new arknet::detail::io_pool(1),
                  [](arknet::detail::io_pool* pool) { arknet::detail::iopool_deleter{}(pool); }) {
            if (!options.max_connections || !options.max_waiters || options.acquire_timeout.count() <= 0 ||
                options.reset_timeout.count() <= 0)
            {
                throw std::invalid_argument("Redis pool limits and deadlines must be positive");
            }
            arknet::detail::pool_cleanup();
            if (!runtime->start())
            {
                throw arknet::system_error(arknet::get_last_error());
            }
        }

        auto executor() const {
            return runtime->get(0)->executor();
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

        bool contains(const std::shared_ptr<entry>& connection) const {
            return std::find(entries.begin(), entries.end(), connection) != entries.end();
        }

        void discard(const std::shared_ptr<entry>& connection) {
            std::erase(entries, connection);
            // close() can synchronously complete the connect handler on this IO lane.
            // Remove membership first so that late completion cannot win cancellation.
            connection->client->close();
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
            if (std::exchange(request->admitted, false))
            {
                --admitted;
            }
            auto handler = std::exchange(request->handler, {});
            if (handler)
            {
                handler(std::move(response));
            }
        }

        lease make_lease(const std::shared_ptr<entry>& connection) {
            connection->phase = entry_phase::leased;
            auto control = std::make_shared<lease_control>();
            control->connection = connection;
            control->return_connection = [weak = this->weak_from_this()](
                                             std::shared_ptr<entry> current, bool unhealthy) {
                if (auto self = weak.lock())
                {
                    asio::post(self->executor(),
                        [self, current = std::move(current), unhealthy] { self->returned(current, unhealthy); });
                }
                else
                {
                    current->client->close();
                }
            };
            return lease(std::move(control));
        }

        void acquire(std::shared_ptr<waiter> request, std::chrono::steady_clock::time_point deadline) {
            if (closed || close_requested)
            {
                complete(request, acquire_result{client_errc::closed});
                return;
            }
            if (!request->admitted)
            {
                complete(request, acquire_result{client_errc::queue_full});
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                complete(request, acquire_result{client_errc::timeout});
                return;
            }
            request->timer = std::make_shared<asio::steady_timer>(executor());
            request->timer->expires_at(deadline);
            request->timer->async_wait(
                [weak = this->weak_from_this(), request = std::weak_ptr(request)](arknet::error_code error) {
                    if (!error)
                    {
                        if (auto self = weak.lock())
                        {
                            if (auto current = request.lock())
                            {
                                self->cancel(current, client_errc::timeout);
                            }
                        }
                    }
                });
            pending.emplace(request->id, request);
            queue.push_back(request);
            pump();
        }

        void cancel(std::shared_ptr<waiter> request, client_errc reason) {
            if (request->completed)
            {
                return;
            }
            if (auto assigned = request->assigned.lock())
            {
                discard(assigned);
            }
            complete(request, acquire_result{reason});
            pump();
        }

        void pump() {
            while (!closed && !close_requested && !queue.empty())
            {
                auto request = queue.front();
                if (request->completed)
                {
                    queue.pop_front();
                    continue;
                }
                auto idle = std::find_if(entries.begin(), entries.end(),
                    [](const auto& connection) { return connection->phase == entry_phase::idle; });
                if (idle != entries.end())
                {
                    auto connection = *idle;
                    if (!connection->client->is_connected())
                    {
                        discard(connection);
                        continue;
                    }
                    queue.pop_front();
                    complete(request, acquire_result{client_errc::none, {}, make_lease(connection)});
                    continue;
                }
                if (entries.size() >= options.max_connections)
                {
                    return;
                }
                queue.pop_front();
                auto connection = std::make_shared<entry>();
                try
                {
                    connection->client = std::shared_ptr<client_type>(
                        new client_type(options.client_configuration, options.certificate_authority, runtime));
                }
                catch (const arknet::system_error& error)
                {
                    complete(request, acquire_result{client_errc::connection_failed, error.code()});
                    continue;
                }
                catch (const std::invalid_argument&)
                {
                    complete(request, acquire_result{client_errc::invalid_command});
                    continue;
                }
                connection->waiting = request;
                request->assigned = connection;
                entries.push_back(connection);
                connection->client->async_connect(
                    options.connection, [weak = this->weak_from_this(), connection](result response) mutable {
                        if (auto self = weak.lock(); self && self->contains(connection))
                        {
                            auto request = connection->waiting.lock();
                            if (!response || self->closed || self->close_requested)
                            {
                                self->discard(connection);
                                if (request)
                                {
                                    self->complete(request,
                                        acquire_result{self->closed || self->close_requested ? client_errc::closed
                                                                                             : response.error,
                                            response.transport_error, {}, std::move(response.replies)});
                                }
                            }
                            else if (request && !request->completed)
                            {
                                self->complete(
                                    request, acquire_result{client_errc::none, {}, self->make_lease(connection)});
                            }
                            else
                            {
                                connection->phase = entry_phase::idle;
                            }
                            self->pump();
                        }
                    });
            }
        }

        void returned(const std::shared_ptr<entry>& connection, bool unhealthy) {
            if (!contains(connection))
            {
                connection->client->close();
                return;
            }
            if (closed || unhealthy || !connection->client->is_connected())
            {
                discard(connection);
                pump();
                return;
            }
            connection->phase = entry_phase::resetting;
            connection->client->async_reset(
                [weak = this->weak_from_this(), connection](result response) {
                    if (auto self = weak.lock(); self && self->contains(connection))
                    {
                        if (!response || self->closed || self->close_requested)
                        {
                            self->discard(connection);
                        }
                        else
                        {
                            connection->phase = entry_phase::idle;
                        }
                        self->pump();
                    }
                },
                options.reset_timeout);
        }

        void shutdown() {
            if (std::exchange(closed, true))
            {
                return;
            }
            auto waiting = std::move(pending);
            pending.clear();
            queue.clear();
            auto connections = std::move(entries);
            entries.clear();
            for (const auto& [id, request] : waiting)
            {
                complete(request, acquire_result{client_errc::closed});
            }
            for (const auto& connection : connections)
            {
                connection->client->close();
            }
        }
    };

public:
    explicit basic_pool(pool_options options = {})
        : state_(std::make_shared<state>(std::move(options))) {}

    basic_pool(const basic_pool&) = delete;
    basic_pool& operator=(const basic_pool&) = delete;

    ~basic_pool() {
        close();
    }

    request_id async_acquire(
        completion_handler handler, std::chrono::milliseconds timeout = std::chrono::milliseconds::zero()) {
        auto self = state_;
        auto request = std::make_shared<waiter>();
        request->id = self->next_id.fetch_add(1);
        request->handler = std::move(handler);
        request->admitted = self->reserve();
        auto deadline =
            std::chrono::steady_clock::now() + (timeout.count() > 0 ? timeout : self->options.acquire_timeout);
        asio::post(self->executor(), [self, request, deadline] { self->acquire(request, deadline); });
        return request->id;
    }

    void cancel(request_id id) {
        auto self = state_;
        asio::post(self->executor(), [self, id] {
            if (auto found = self->pending.find(id); found != self->pending.end())
            {
                self->cancel(found->second, client_errc::cancelled);
            }
        });
    }

    void close() {
        auto self = state_;
        if (!self->close_requested.exchange(true))
        {
            asio::dispatch(self->executor(), [self] { self->shutdown(); });
        }
    }

private:
    std::shared_ptr<state> state_;
};

using pool = basic_pool<arknet::tcp_client>;
#if defined(ARKNET_ENABLE_SSL)
using tls_pool = basic_pool<arknet::tcps_client>;
#endif

} // namespace arknet::redis
