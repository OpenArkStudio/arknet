// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

struct pool_diagnostics
{
    std::uint64_t return_submission_failures{};
    std::exception_ptr last_return_submission_exception;
};

// Exclusive leases preserve transactions and connection state. Returning a lease runs RESET,
// then restores the configured authentication/protocol/database before another borrower can use it.
// Servers without RESET can still be used: their connections are discarded on return.
template<class Transport>
class basic_pool
{
    using client_type = basic_client<Transport>;
    struct waiter;

    struct publication_gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::size_t publishers{};
        bool terminating{};
        bool terminated{};
        std::thread::id finalizer;
        std::exception_ptr failure;
    };

    class publication
    {
        inline static thread_local publication* current_{};
        publication_gate* owner_{};
        publication* previous_{};

    public:
        explicit publication(publication_gate& gate) {
            std::lock_guard lock(gate.mutex);
            if (!gate.terminating)
            {
                ++gate.publishers;
                owner_ = &gate;
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
                    std::lock_guard lock(owner_->mutex);
                    --owner_->publishers;
                }
                owner_->changed.notify_all();
            }
        }

        explicit operator bool() const noexcept {
            return owner_ != nullptr;
        }

        static bool active_on_this_thread(const publication_gate& gate) noexcept {
            for (auto* current = current_; current; current = current->previous_)
            {
                if (current->owner_ == &gate)
                {
                    return true;
                }
            }
            return false;
        }
    };

    enum class entry_phase
    {
        connecting,
        idle,
        leased,
        resetting,
        retiring
    };

    struct entry
    {
        std::shared_ptr<client_type> client;
        entry_phase phase{entry_phase::connecting};
        std::weak_ptr<waiter> waiting;
        std::atomic_bool failed_return{};
    };

    struct return_diagnostics
    {
        mutable std::mutex mutex;
        pool_diagnostics info;

        void record(std::exception_ptr error) noexcept {
            std::lock_guard lock(mutex);
            if (info.return_submission_failures != (std::numeric_limits<std::uint64_t>::max)())
            {
                ++info.return_submission_failures;
            }
            info.last_return_submission_exception = std::move(error);
        }

        pool_diagnostics snapshot() const {
            std::lock_guard lock(mutex);
            return info;
        }
    };

    struct lease_control
    {
        std::shared_ptr<entry> connection;
        std::shared_ptr<publication_gate> gate;
        std::function<void(std::shared_ptr<entry>, bool)> return_connection;
        std::mutex mutex;
        std::size_t pending{};
        bool released{};
        bool discard{};

        void finish() {
            publication publishing(*gate);
            std::function<void(std::shared_ptr<entry>, bool)> returning;
            bool unhealthy{};
            {
                std::lock_guard lock(mutex);
                --pending;
                if (publishing && released && !pending)
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
            publication publishing(*gate);
            std::function<void(std::shared_ptr<entry>, bool)> returning;
            bool discard_connection{};
            {
                std::lock_guard lock(mutex);
                released = true;
                discard = discard || unhealthy;
                if (publishing && !pending)
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
            publication publishing(*control->gate);
            if (!publishing)
            {
                return client_type::reject_terminated(std::move(handler));
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
            const auto control = control_;
            if (control)
            {
                publication publishing(*control->gate);
                if (!publishing)
                {
                    return;
                }
                std::lock_guard lock(control->mutex);
                if (!control->released)
                {
                    control->connection->client->cancel(id);
                }
            }
        }

        void release() {
            const auto control = control_;
            if (control)
            {
                control->release();
            }
        }

        void discard() {
            const auto control = control_;
            if (control)
            {
                control->release(true);
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
        std::chrono::steady_clock::time_point deadline;
        bool completed{};
        bool admitted{};
    };

    struct state : std::enable_shared_from_this<state>
    {
        pool_options options;
        std::shared_ptr<arknet::detail::io_pool> runtime;
        std::shared_ptr<publication_gate> gate{std::make_shared<publication_gate>()};
        std::deque<std::shared_ptr<waiter>> queue;
        std::unordered_map<request_id, std::shared_ptr<waiter>> pending;
        std::vector<std::shared_ptr<entry>> entries;
        // Weak history includes discarded connections still retained by leases.
        // It owns neither an executor nor a past connection's runtime.
        std::vector<std::weak_ptr<typename client_type::state>> children;
        std::vector<std::weak_ptr<lease_control>> controls;
        std::atomic<request_id> next_id{1};
        std::atomic_size_t admitted{};
        std::atomic_bool close_requested{};
        std::shared_ptr<return_diagnostics> diagnostics = std::make_shared<return_diagnostics>();
        bool recovering_returns{};
        bool closed{};

        explicit state(pool_options configuration)
            : options(std::move(configuration))
            , runtime(new arknet::detail::io_pool(1),
                  [](arknet::detail::io_pool* pool) { arknet::detail::iopool_deleter{}(pool); }) {
            const auto now = std::chrono::steady_clock::now();
            if (!options.max_connections || !options.max_waiters ||
                !arknet::detail::checked_deadline(now, options.acquire_timeout) ||
                !arknet::detail::checked_deadline(now, options.reset_timeout))
            {
                throw std::invalid_argument("Redis pool limits and deadlines must be positive and representable");
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
            return connection->phase != entry_phase::retiring &&
                   std::find(entries.begin(), entries.end(), connection) != entries.end();
        }

        void recover_failed_returns() {
            if (closed || close_requested || recovering_returns)
            {
                return;
            }
            recovering_returns = true;
            // Only the existing bounded entry set is scanned. Keep failed closes
            // tracked, and leave their next attempt to a later actor pass.
            for (std::size_t index{}, remaining = entries.size(); remaining && index < entries.size(); --remaining)
            {
                auto connection = entries[index];
                if (!connection->failed_return.load())
                {
                    ++index;
                    continue;
                }
                // close() can synchronously complete a connect/reset callback.
                // Suppress membership before calling it without losing ownership
                // if its executor submission throws.
                connection->phase = entry_phase::retiring;
                try
                {
                    connection->client->close();
                    std::erase(entries, connection);
                }
                catch (...)
                {
                    diagnostics->record(std::current_exception());
                    ++index;
                }
            }
            recovering_returns = false;
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
            std::erase_if(controls, [](const auto& current) { return current.expired(); });
            connection->phase = entry_phase::leased;
            auto control = std::make_shared<lease_control>();
            control->connection = connection;
            control->gate = gate;
            control->return_connection = [weak = this->weak_from_this(), diagnostics = diagnostics](
                                             std::shared_ptr<entry> current, bool unhealthy) noexcept {
                auto self = weak.lock();
                try
                {
                    if (self)
                    {
                        // Keep current pinned across post: a throwing submission
                        // may already have consumed its handler's moved captures.
                        asio::post(
                            self->executor(), [self, current, unhealthy] { self->returned(current, unhealthy); });
                    }
                    else
                    {
                        current->client->close();
                    }
                }
                catch (...)
                {
                    current->failed_return = true;
                    diagnostics->record(std::current_exception());
                }
            };
            controls.push_back(control);
            return lease(std::move(control));
        }

        void acquire(std::shared_ptr<waiter> request, std::chrono::steady_clock::time_point deadline) {
            request->deadline = deadline;
            recover_failed_returns();
            if (closed || close_requested)
            {
                complete(request, acquire_result{client_errc::closed});
                return;
            }
            if (!request->admitted)
            {
                complete(request, acquire_result{client_errc::queue_full});
                pump(false);
                return;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                complete(request, acquire_result{client_errc::timeout});
                pump(false);
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
            pump(false);
        }

        void cancel(std::shared_ptr<waiter> request, client_errc reason) {
            recover_failed_returns();
            if (request->completed)
            {
                pump(false);
                return;
            }
            if (auto assigned = request->assigned.lock())
            {
                discard(assigned);
            }
            complete(request, acquire_result{reason});
            pump(false);
        }

        void pump(bool recover = true) {
            if (recover)
            {
                recover_failed_returns();
            }
            std::erase_if(children, [](const auto& current) { return current.expired(); });
            while (!closed && !close_requested && !queue.empty())
            {
                auto request = queue.front();
                if (request->completed)
                {
                    queue.pop_front();
                    continue;
                }
                if (std::chrono::steady_clock::now() >= request->deadline)
                {
                    queue.pop_front();
                    complete(request, acquire_result{client_errc::timeout});
                    continue;
                }
                auto idle = std::find_if(entries.begin(), entries.end(), [](const auto& connection) {
                    return connection->phase == entry_phase::idle && !connection->failed_return.load();
                });
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
                children.push_back(connection->client->state_);
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
                                if (std::chrono::steady_clock::now() >= request->deadline)
                                {
                                    self->discard(connection);
                                    self->complete(request, acquire_result{client_errc::timeout});
                                }
                                else
                                {
                                    self->complete(
                                        request, acquire_result{client_errc::none, {}, self->make_lease(connection)});
                                }
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
            if (connection->failed_return.load())
            {
                pump();
                return;
            }
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

        bool close_and_wait() {
            {
                std::unique_lock lock(gate->mutex);
                if ((runtime && runtime->running_in_threads()) || publication::active_on_this_thread(*gate) ||
                    gate->finalizer == std::this_thread::get_id())
                {
                    arknet::set_last_error(asio::error::operation_not_supported);
                    return false;
                }
                if (gate->terminating)
                {
                    gate->changed.wait(lock, [&] { return gate->terminated || gate->failure; });
                    if (gate->failure)
                    {
                        std::rethrow_exception(gate->failure);
                    }
                    arknet::clear_last_error();
                    return true;
                }
                gate->terminating = true;
                gate->finalizer = std::this_thread::get_id();
                gate->changed.wait(lock, [&] { return gate->publishers == 0; });
            }
            try
            {
                close_requested = true;
                // Only the pool actor creates children/leases. The closing
                // flags and same-lane fence prevent any later catalog writer.
                using retirement = std::pair<std::vector<std::shared_ptr<typename client_type::state>>,
                    std::vector<std::weak_ptr<lease_control>>>;
                auto completion = std::make_shared<std::promise<retirement>>();
                auto ready = completion->get_future();
                const auto self = this->shared_from_this();
                asio::post(executor(), [self, completion] {
                    try
                    {
                        // Pin before shutdown can release the last entry or
                        // invoke a callback that destroys its lease/facade.
                        std::vector<std::shared_ptr<typename client_type::state>> children;
                        for (auto& weak : self->children)
                        {
                            if (auto child = weak.lock())
                            {
                                children.push_back(std::move(child));
                            }
                        }
                        self->shutdown();
                        self->children.clear();
                        completion->set_value({std::move(children), std::move(self->controls)});
                    }
                    catch (...)
                    { completion->set_exception(std::current_exception()); }
                });
                auto [children, controls] = ready.get();
                for (auto& child : children)
                {
                    {
                        std::unique_lock lock(child->publishing_mutex);
                        child->terminating = true;
                        child->finalizer = std::this_thread::get_id();
                        child->publishing_changed.wait(lock, [&] { return child->publishers == 0; });
                    }
                    child->request_close();
                }
                auto owned_runtime = runtime;
                owned_runtime->stop();

                // Accepted outer Asio handlers/captures have retired. Retained
                // leases become inert without keeping TLS, timers or contexts.
                for (auto& child : children)
                {
                    child->retire_context_resources();
                    {
                        std::lock_guard lock(child->publishing_mutex);
                        child->terminated = true;
                        child->finalizer = {};
                    }
                    child->publishing_changed.notify_all();
                }
                for (auto& weak : controls)
                {
                    if (auto control = weak.lock())
                    {
                        std::lock_guard lock(control->mutex);
                        control->released = true;
                        control->return_connection = {};
                    }
                }
                queue.clear();
                pending.clear();
                entries.clear();
                options = {};
                {
                    std::lock_guard lock(gate->mutex);
                    runtime.reset();
                }
                owned_runtime.reset();
                {
                    std::lock_guard lock(gate->mutex);
                    gate->terminated = true;
                    gate->finalizer = {};
                }
                gate->changed.notify_all();
                arknet::clear_last_error();
                return true;
            }
            catch (...)
            {
                {
                    std::lock_guard lock(gate->mutex);
                    gate->failure = std::current_exception();
                    gate->finalizer = {};
                }
                gate->changed.notify_all();
                throw;
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
        publication publishing(*self->gate);
        if (!publishing)
        {
            if (handler)
            {
                handler(acquire_result{client_errc::closed});
            }
            return 0;
        }
        auto request = std::make_shared<waiter>();
        request->id = self->next_id.fetch_add(1);
        request->handler = std::move(handler);
        const auto deadline = arknet::detail::checked_deadline(std::chrono::steady_clock::now(),
            timeout == std::chrono::milliseconds::zero() ? self->options.acquire_timeout : timeout);
        request->admitted = deadline && self->reserve();
        try
        {
            asio::post(self->executor(), [self, request, deadline] {
                if (!deadline)
                {
                    self->complete(request, acquire_result{client_errc::invalid_command});
                }
                else
                {
                    self->acquire(request, *deadline);
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
        publication publishing(*self->gate);
        if (!publishing)
        {
            return;
        }
        asio::post(self->executor(), [self, id] {
            if (auto found = self->pending.find(id); found != self->pending.end())
            {
                self->cancel(found->second, client_errc::cancelled);
            }
            else
            {
                self->pump();
            }
        });
    }

    pool_diagnostics diagnostics() const {
        return state_->diagnostics->snapshot();
    }

    void close() {
        auto self = state_;
        publication publishing(*self->gate);
        if (!publishing)
        {
            return;
        }
        if (!self->close_requested.exchange(true))
        {
            asio::dispatch(self->executor(), [self] { self->shutdown(); });
        }
    }

    // Permanently closes admission, retires accepted callbacks and captures,
    // joins this pool's owned worker and makes retained leases inert. Call only
    // from a non-library-IO thread; ordinary close() remains asynchronous.
    bool close_and_wait() {
        const auto self = state_;
        return self->close_and_wait();
    }

private:
    std::shared_ptr<state> state_;
};

using pool = basic_pool<arknet::tcp_client>;
#if defined(ARKNET_ENABLE_SSL)
using tls_pool = basic_pool<arknet::tcps_client>;
#endif

} // namespace arknet::redis
