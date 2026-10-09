// Copyright (c) 2026 OpenArkStudio
// Distributed under BSL-1.0. See LICENSE.
#pragma once
#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>
#include <arknet/base/io_pool.hpp>
#include <arknet/base/define.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/shared_mutex.hpp>
#include <arknet/base/detail/util.hpp>
namespace arknet::detail
{
template <class Session> class session_mgr_t
{
    friend Session;
    ARKNET_CLASS_FRIEND_DECLARE_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SERVER;
    ARKNET_CLASS_FRIEND_DECLARE_TCP_SESSION;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_BASE;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_SERVER;
    ARKNET_CLASS_FRIEND_DECLARE_UDP_SESSION;

public:
    using self = session_mgr_t;
    using args_type = typename Session::args_type;
    using key_type = typename Session::key_type;
    explicit session_mgr_t(std::shared_ptr<io_t> acceptor, std::atomic<state_t>& state)
        : io_(std::move(acceptor)), state_(state)
    {
        sessions_.reserve(64);
    }
    template <class Callback> void emplace(std::shared_ptr<Session> session, Callback&& callback)
    {
        if (!session)
            return;
        dispatch(
            [this, session = std::move(session), callback = std::forward<Callback>(callback)]() mutable
            {
                bool inserted = false;
                {
                    arknet::unique_locker lock(mutex_);
                    if (state_.load() == state_t::started)
                        inserted = sessions_.try_emplace(session->hash_key(), session).second;
                }
                callback(inserted);
            });
    }
    template <class Callback> void erase(std::shared_ptr<Session> session, Callback&& callback)
    {
        if (!session)
            return;
        dispatch(
            [this, session = std::move(session), callback = std::forward<Callback>(callback)]() mutable
            {
                bool erased;
                {
                    arknet::unique_locker lock(mutex_);
                    const auto found = sessions_.find(session->hash_key());
                    erased = found != sessions_.end() && found->second == session;
                    if (erased)
                        sessions_.erase(found);
                }
                callback(erased);
            });
    }
    template <class Callable> void post(Callable&& callback)
    {
        asio::post(io_->executor(), make_allocator(allocator_, std::forward<Callable>(callback)));
    }
    template <class Callable> void dispatch(Callable&& callback)
    {
        asio::dispatch(io_->executor(), make_allocator(allocator_, std::forward<Callable>(callback)));
    }
    template <class Callable> void for_each(Callable&& callback)
    {
        for (auto& session : snapshot())
            callback(session);
    }
    template <class Callable> void quick_for_each(Callable&& callback)
    {
        arknet::shared_locker lock(mutex_);
        for (auto& [key, session] : sessions_)
            callback(session);
    }
    std::shared_ptr<Session> find(const key_type& key)
    {
        arknet::shared_locker lock(mutex_);
        const auto found = sessions_.find(key);
        return found == sessions_.end() ? nullptr : found->second;
    }
    template <class Predicate> std::shared_ptr<Session> find_if(Predicate&& predicate)
    {
        for (auto& session : snapshot())
            if (predicate(session))
                return session;
        return {};
    }
    std::size_t size() const noexcept
    {
        arknet::shared_locker lock(mutex_);
        return sessions_.size();
    }
    bool empty() const noexcept { return size() == 0; }
    io_t& io() noexcept { return *io_; }
    const io_t& io() const noexcept { return *io_; }

protected:
    mutable arknet::shared_mutexer mutex_;
    std::unordered_map<key_type, std::shared_ptr<Session>> sessions_;
    std::shared_ptr<io_t> io_;
    handler_memory<std::false_type, assizer<args_type>> allocator_;
    std::atomic<state_t>& state_;
#ifndef NDEBUG
    bool is_all_session_stop_called_ = false;
#endif
private:
    auto snapshot()
    {
        std::vector<std::shared_ptr<Session>> result;
        arknet::shared_locker lock(mutex_);
        result.reserve(sessions_.size());
        for (const auto& [key, session] : sessions_)
            result.push_back(session);
        return result;
    }
};
}
