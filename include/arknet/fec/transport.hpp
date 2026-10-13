// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arknet/thirdparty/fecpp.hpp>

namespace arknet::fec
{
enum class policy
{
    disabled,
    fixed,
    adaptive
};
enum class change_reason
{
    configured,
    loss,
    healthy,
    overload
};
inline const char* reason_name(change_reason reason) noexcept
{
    switch (reason)
    {
    case change_reason::loss:
        return "loss";
    case change_reason::healthy:
        return "healthy";
    case change_reason::overload:
        return "overload";
    default:
        return "configured";
    }
}
struct options
{
    policy mode = policy::disabled;
    std::uint8_t data_shards = 8;
    std::uint8_t parity_shards = 2;
    std::uint8_t max_parity_shards = 4;
    std::uint8_t min_data_shards = 2;
    std::uint32_t group_wait_ms = 2;
    std::uint32_t group_lifetime_ms = 1000;
    std::size_t max_groups = 128;
    std::size_t max_buffer_bytes = 1024 * 1024;
    unsigned max_redundancy_percent = 50;
    std::uint32_t sample_window_ms = 1000;
    std::uint32_t cooldown_ms = 2000;
    std::uint32_t min_samples = 64;
    unsigned enable_loss_percent = 3;
    unsigned disable_loss_percent = 1;
    unsigned stable_windows = 3;
    unsigned overload_queue_percent = 75;
    static options fixed()
    {
        options value;
        value.mode = policy::fixed;
        return value;
    }
    static options adaptive()
    {
        options value;
        value.mode = policy::adaptive;
        return value;
    }
    void validate() const
    {
        if ((mode != policy::disabled && mode != policy::fixed && mode != policy::adaptive) || !data_shards ||
            data_shards > 32 || !max_parity_shards || max_parity_shards > 16 || parity_shards > max_parity_shards ||
            !min_data_shards || min_data_shards > data_shards || !group_wait_ms || group_wait_ms > 1000 ||
            group_lifetime_ms < group_wait_ms || group_lifetime_ms > 60000 || !max_groups || max_groups > 1024 ||
            max_buffer_bytes < 4096 || max_buffer_bytes > 64 * 1024 * 1024 || max_redundancy_percent > 100 ||
            !sample_window_ms || sample_window_ms > 60000 || cooldown_ms > 60000 || !min_samples ||
            !enable_loss_percent || enable_loss_percent > 100 || disable_loss_percent >= enable_loss_percent ||
            !stable_windows || !overload_queue_percent || overload_queue_percent > 100)
            throw std::invalid_argument("invalid FEC options");
    }
};
struct statistics
{
    std::uint64_t data_packets_tx{}, parity_packets_tx{}, wire_bytes_tx{}, parity_bytes_tx{}, wire_bytes_rx{};
    std::uint64_t groups_rx{}, data_shards_rx{}, missing_shards{}, recovered_shards{}, duplicates{};
    std::uint64_t invalid_packets{}, expired_groups{}, evicted_groups{}, policy_changes{};
    std::size_t retained_bytes{}, retained_groups{};
    std::uint8_t current_parity_shards{};
    change_reason last_change = change_reason::configured;
};
namespace detail
{
inline std::uint64_t read(std::string_view bytes, std::size_t offset, unsigned count) noexcept
{
    std::uint64_t value{};
    for (unsigned i = 0; i < count; ++i)
        value |= std::uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    return value;
}
inline void write(std::string& bytes, std::size_t offset, std::uint64_t value, unsigned count) noexcept
{
    for (unsigned i = 0; i < count; ++i)
        bytes[offset + i] = static_cast<char>(value >> (8 * i));
}
inline std::int32_t difference(std::uint32_t a, std::uint32_t b) noexcept
{
    return std::bit_cast<std::int32_t>(a - b);
}
inline std::uint32_t checksum(std::string_view bytes) noexcept
{
    static constexpr auto table = []
    {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            auto value = i;
            for (unsigned bit = 0; bit < 8; ++bit)
                value = (value >> 1) ^ ((value & 1) ? 0xedb88320U : 0U);
            values[i] = value;
        }
        return values;
    }();
    std::uint32_t value = 0xffffffffU;
    for (unsigned char c : bytes)
        value = table[(value ^ c) & 255] ^ (value >> 8);
    return value ^ 0xffffffffU;
}
}

// Serialize calls. CRC detects corruption; it does not authenticate a peer.
class transport
{
public:
    static constexpr std::size_t header_size = 20, payload_prefix = 6, overhead = header_size + payload_prefix;
    using output = std::function<void(std::string_view)>;
    using delivery = std::function<bool(std::string_view)>;
    transport(std::uint32_t conversation, std::size_t mtu, options settings, output emit, delivery deliver)
        : conversation_(conversation), mtu_(mtu), options_(settings), emit_(std::move(emit)),
          deliver_(std::move(deliver))
    {
        options_.validate();
        if (mtu <= overhead + 24 || mtu > 65507 || !emit_ || !deliver_)
            throw std::invalid_argument("invalid FEC transport");
        stats_.current_parity_shards = options_.mode == policy::fixed ? options_.parity_shards : 0;
    }
    bool send(std::string_view bytes, std::uint32_t now, bool control = false)
    {
        if (stopped_ || bytes.size() < 24 || bytes.size() > mtu_ - overhead)
            return false;
        if (control || !stats_.current_parity_shards)
        {
            emit_group({std::string(bytes)}, 0, control ? 3 : 0);
            return !stopped_;
        }
        if (retained_bytes_ + pending_bytes_ + bytes.size() + payload_prefix > options_.max_buffer_bytes)
        {
            flush();
            if (stopped_)
                return false;
            emit_group({std::string(bytes)}, 0);
            return !stopped_;
        }
        if (pending_.empty())
            pending_since_ = now;
        pending_.emplace_back(bytes);
        pending_bytes_ += bytes.size() + payload_prefix;
        if (pending_.size() >= options_.data_shards)
            flush();
        return !stopped_;
    }
    void update(std::uint32_t now)
    {
        if (stopped_)
            return;
        if (!pending_.empty() &&
            detail::difference(now, pending_since_) >= static_cast<std::int32_t>(options_.group_wait_ms))
            flush();
        if (stopped_)
            return;
        for (auto it = groups_.begin(); it != groups_.end();)
        {
            if (detail::difference(now, it->second.created) >= static_cast<std::int32_t>(options_.group_lifetime_ms))
            {
                finalize(it->second);
                retire(it->first);
                retained_bytes_ -= group_cost + it->second.bytes;
                it = groups_.erase(it);
            }
            else
                ++it;
        }
        if (!feedback_started_ ||
            detail::difference(now, feedback_at_) >= static_cast<std::int32_t>(options_.sample_window_ms))
        {
            feedback_started_ = true;
            feedback_at_ = now;
            auto bytes = header(2, 0, 0, 0, 24, ++feedback_sequence_);
            bytes.resize(header_size + 24);
            detail::write(bytes, header_size, stats_.data_shards_rx, 8);
            detail::write(bytes, header_size + 8, stats_.missing_shards, 8);
            detail::write(bytes, header_size + 16, finalized_recovered_, 8);
            emit_wire(bytes, false, false);
        }
    }
    void adapt(std::uint32_t now, std::uint64_t transmissions, std::uint64_t retransmissions, std::uint32_t rtt_ms,
               std::size_t queued_bytes, std::size_t queue_limit)
    {
        if (stopped_ || options_.mode != policy::adaptive)
            return;
        if (!sampling_)
        {
            sampling_ = true;
            sample_at_ = now;
            last_transmissions_ = transmissions;
            last_retransmissions_ = retransmissions;
            return;
        }
        if (detail::difference(now, sample_at_) < static_cast<std::int32_t>(options_.sample_window_ms))
            return;
        sample_at_ = now;
        const auto local_samples = transmissions >= last_transmissions_ ? transmissions - last_transmissions_ : 0;
        const auto local_lost = retransmissions >= last_retransmissions_ ? retransmissions - last_retransmissions_ : 0;
        const auto peer_samples = peer_data_ - last_peer_data_;
        const auto peer_lost = peer_missing_ - last_peer_missing_;
        last_transmissions_ = transmissions;
        last_retransmissions_ = retransmissions;
        last_peer_data_ = peer_data_;
        last_peer_missing_ = peer_missing_;
        if (rtt_ms && (!minimum_rtt_ || rtt_ms < minimum_rtt_))
            minimum_rtt_ = rtt_ms;
        const bool overloaded =
            (queue_limit && static_cast<double>(queued_bytes) / queue_limit * 100 >= options_.overload_queue_percent) ||
            (minimum_rtt_ && rtt_ms > std::uint64_t(minimum_rtt_) * 2 && queued_bytes > previous_queued_bytes_);
        previous_queued_bytes_ = queued_bytes;
        if (overloaded)
        {
            change(0, change_reason::overload, now);
            stable_ = 0;
            return;
        }
        if ((local_samples < options_.min_samples && peer_samples < options_.min_samples) ||
            (changed_ && detail::difference(now, changed_at_) < static_cast<std::int32_t>(options_.cooldown_ms)))
            return;
        const auto ratio = [&](std::uint64_t lost, std::uint64_t samples)
        {
            return samples < options_.min_samples
                       ? 0.0
                       : std::min(100.0, 100.0 * static_cast<double>(lost) / static_cast<double>(samples));
        };
        const auto percent = std::max(ratio(local_lost, local_samples), ratio(peer_lost, peer_samples));
        if (percent >= options_.enable_loss_percent)
        {
            stable_ = 0;
            const auto desired =
                static_cast<std::uint8_t>(std::clamp(std::ceil(percent * options_.data_shards / 50.0), 1.0,
                                                     static_cast<double>(options_.max_parity_shards)));
            change(std::max(stats_.current_parity_shards, desired), change_reason::loss, now);
        }
        else if (percent <= options_.disable_loss_percent)
        {
            if (++stable_ >= options_.stable_windows)
            {
                if (stats_.current_parity_shards)
                    change(stats_.current_parity_shards - 1, change_reason::healthy, now);
                stable_ = 0;
            }
        }
        else
            stable_ = 0;
    }
    bool input(std::string_view bytes, std::uint32_t now)
    {
        if (stopped_)
            return false;
        stats_.wire_bytes_rx += bytes.size();
        const auto invalid = [&]
        {
            ++stats_.invalid_packets;
            return false;
        };
        if (bytes.size() < header_size || bytes.size() > mtu_ || bytes.substr(0, 4) != "AFEC" ||
            static_cast<unsigned char>(bytes[4]) != 1 || bytes[9] != 0 || detail::read(bytes, 12, 4) != conversation_)
            return invalid();
        const auto kind = static_cast<unsigned char>(bytes[5]);
        const auto k = static_cast<unsigned char>(bytes[6]), p = static_cast<unsigned char>(bytes[7]),
                   index = static_cast<unsigned char>(bytes[8]);
        const auto size = static_cast<std::size_t>(detail::read(bytes, 10, 2));
        const auto id = static_cast<std::uint32_t>(detail::read(bytes, 16, 4));
        if (kind == 2)
        {
            if (k || p || index || size != 24 || bytes.size() != header_size + 24)
                return invalid();
            const auto data = detail::read(bytes, header_size, 8), missing = detail::read(bytes, header_size + 8, 8),
                       recovered = detail::read(bytes, header_size + 16, 8);
            if (missing > data || recovered > missing || data > stats_.data_packets_tx ||
                (have_peer_ && (detail::difference(id, peer_sequence_) <= 0 || data < peer_data_ ||
                                missing < peer_missing_ || recovered < peer_recovered_)))
                return false;
            have_peer_ = true;
            peer_sequence_ = id;
            peer_data_ = data;
            peer_missing_ = missing;
            peer_recovered_ = recovered;
            return true;
        }
        if (kind > 3 || !k || k > 32 || p > 16 || index >= k + p || size < payload_prefix + 24 ||
            size > mtu_ - header_size || ((kind == 0 || kind == 3) && index >= k) || (kind == 1 && index < k) ||
            (kind == 3 && (k != 1 || p || index)) || (!p && (k != 1 || index || kind == 1)))
            return invalid();
        const auto payload = bytes.substr(header_size);
        if (kind == 1 && payload.size() != size)
            return invalid();
        std::string_view original;
        if (kind != 1)
        {
            if (payload.size() < payload_prefix || payload.size() > size)
                return invalid();
            original = unpack(payload);
            if (original.empty() || payload.size() != payload_prefix + original.size())
                return invalid();
        }
        auto it = groups_.find(id);
        if (it != groups_.end() && (it->second.k != k || it->second.p != p || it->second.size != size))
            return invalid();
        if (have_highest_ && detail::difference(id, highest_) < -4095)
            return invalid();
        if (retired_valid_[id % retired_.size()] && retired_[id % retired_.size()] == id)
        {
            ++stats_.duplicates;
            return false;
        }
        if (!have_highest_ || detail::difference(id, highest_) > 0)
        {
            highest_ = id;
            have_highest_ = true;
        }
        if (kind == 3 || !p)
        {
            retire(id);
            if (kind == 0)
            {
                ++stats_.groups_rx;
                ++stats_.data_shards_rx;
            }
            return deliver_packet(original);
        }
        if (it == groups_.end())
        {
            if (groups_.size() >= options_.max_groups ||
                retained_bytes_ + pending_bytes_ + group_cost + size > options_.max_buffer_bytes)
            {
                ++stats_.evicted_groups;
                return kind != 1 && deliver_packet(original);
            }
            group value;
            value.k = k;
            value.p = p;
            value.size = size;
            value.created = now;
            it = groups_.emplace(id, std::move(value)).first;
            retained_bytes_ += group_cost;
            ++stats_.groups_rx;
        }
        auto& value = it->second;
        if (value.present[index])
        {
            ++stats_.duplicates;
            return false;
        }
        if (retained_bytes_ + pending_bytes_ + size > options_.max_buffer_bytes)
        {
            retained_bytes_ -= group_cost + value.bytes;
            groups_.erase(it);
            ++stats_.evicted_groups;
            return kind != 1 && deliver_packet(original);
        }
        value.present[index] = true;
        ++value.count;
        std::vector<std::string> deliveries;
        if (kind != 1 && !value.delivered[index])
        {
            deliveries.emplace_back(original);
            value.delivered[index] = true;
        }
        value.shards[index] = std::string(size, '\0');
        std::copy(payload.begin(), payload.end(), value.shards[index].begin());
        value.bytes += size;
        retained_bytes_ += size;
        if (value.count >= k)
        {
            std::map<std::size_t, const std::uint8_t*> shares;
            for (unsigned i = 0; i < k + p; ++i)
                if (value.present[i])
                    shares.emplace(i, reinterpret_cast<const std::uint8_t*>(value.shards[i].data()));
            std::vector<std::pair<unsigned, std::string>> recovered;
            try
            {
                if (std::count(value.delivered.begin(), value.delivered.begin() + k, true) != k)
                {
                    arknet::detail::fecpp::fec_code code(k, k + p);
                    code.decode(shares, size,
                                [&](std::size_t i, std::size_t, const std::uint8_t* data, std::size_t count)
                                {
                                    if (value.delivered[i])
                                        return;
                                    const auto message = unpack({reinterpret_cast<const char*>(data), count});
                                    if (message.empty())
                                        throw std::invalid_argument("corrupt recovered FEC shard");
                                    recovered.emplace_back(static_cast<unsigned>(i), message);
                                });
                }
            }
            catch (const std::exception&)
            {
                retained_bytes_ -= group_cost + value.bytes;
                groups_.erase(it);
                ++stats_.invalid_packets;
                for (const auto& message : deliveries)
                    if (!deliver_packet(message))
                        return false;
                return !deliveries.empty();
            }
            for (const auto& [i, message] : recovered)
            {
                deliveries.push_back(message);
                value.delivered[i] = true;
                ++stats_.recovered_shards;
                ++value.recovered;
            }
            value.decoded = true;
            finalize(value);
            retire(id);
            retained_bytes_ -= group_cost + value.bytes;
            groups_.erase(it);
        }
        // All group references are released before callbacks can stop the channel.
        for (const auto& message : deliveries)
            if (!deliver_packet(message))
                return false;
        return true;
    }
    statistics snapshot() const noexcept
    {
        auto value = stats_;
        value.retained_bytes = retained_bytes_ + pending_bytes_;
        value.retained_groups = groups_.size();
        return value;
    }
    void stop()
    {
        stopped_ = true;
        pending_.clear();
        groups_.clear();
        retained_bytes_ = 0;
        pending_bytes_ = 0;
    }

private:
    bool deliver_packet(std::string_view bytes) { return !stopped_ && deliver_(bytes) && !stopped_; }
    struct group
    {
        unsigned k{}, p{}, count{}, recovered{};
        std::size_t size{}, bytes{};
        std::uint32_t created{};
        bool decoded{};
        std::array<std::string, 48> shards;
        std::array<bool, 48> present{};
        std::array<bool, 32> delivered{};
    };
    static constexpr std::size_t group_cost = sizeof(group) + 64;
    static std::string_view unpack(std::string_view bytes)
    {
        if (bytes.size() < payload_prefix)
            return {};
        const auto length = static_cast<std::size_t>(detail::read(bytes, 0, 2));
        if (length < 24 || length > bytes.size() - payload_prefix)
            return {};
        const auto message = bytes.substr(payload_prefix, length);
        return detail::checksum(message) == detail::read(bytes, 2, 4) ? message : std::string_view{};
    }
    std::string header(unsigned kind, unsigned k, unsigned p, unsigned index, std::size_t size,
                       std::uint32_t group_id) const
    {
        std::string bytes(header_size, '\0');
        bytes.replace(0, 4, "AFEC");
        bytes[4] = 1;
        bytes[5] = static_cast<char>(kind);
        bytes[6] = static_cast<char>(k);
        bytes[7] = static_cast<char>(p);
        bytes[8] = static_cast<char>(index);
        detail::write(bytes, 10, size, 2);
        detail::write(bytes, 12, conversation_, 4);
        detail::write(bytes, 16, group_id, 4);
        return bytes;
    }
    void emit_wire(std::string_view bytes, bool data, bool parity)
    {
        if (stopped_)
            return;
        stats_.wire_bytes_tx += bytes.size();
        if (data)
            ++stats_.data_packets_tx;
        if (parity)
        {
            ++stats_.parity_packets_tx;
            stats_.parity_bytes_tx += bytes.size();
        }
        emit_(bytes);
    }
    void emit_group(const std::vector<std::string>& messages, unsigned parity, unsigned kind = 0)
    {
        if (stopped_ || messages.empty())
            return;
        std::size_t size{}, wire_size{};
        for (const auto& bytes : messages)
        {
            size = std::max(size, bytes.size() + payload_prefix);
            wire_size += overhead + bytes.size();
        }
        parity =
            std::min<std::size_t>(parity, wire_size * options_.max_redundancy_percent / 100 / (header_size + size));
        if (messages.size() < options_.min_data_shards)
            parity = 0;
        if (!parity)
        {
            for (const auto& message : messages)
            {
                if (stopped_)
                    break;
                std::string payload(payload_prefix, '\0');
                detail::write(payload, 0, message.size(), 2);
                detail::write(payload, 2, detail::checksum(message), 4);
                payload += message;
                emit_wire(header(kind, 1, 0, 0, payload.size(), next_group_++) + payload, kind == 0, false);
            }
            return;
        }
        const auto k = messages.size();
        const auto id = next_group_++;
        std::vector<std::uint8_t> data(k * size);
        for (std::size_t i = 0; i < k; ++i)
        {
            if (stopped_)
                return;
            std::string payload(payload_prefix, '\0');
            detail::write(payload, 0, messages[i].size(), 2);
            detail::write(payload, 2, detail::checksum(messages[i]), 4);
            payload += messages[i];
            std::copy(payload.begin(), payload.end(), data.begin() + i * size);
            auto bytes = header(kind, k, parity, i, size, id) + payload;
            emit_wire(bytes, kind == 0, false);
        }
        if (stopped_)
            return;
        arknet::detail::fecpp::fec_code code(k, k + parity);
        code.encode(data.data(), data.size(),
                    [&](std::size_t i, std::size_t, const std::uint8_t* data, std::size_t count)
                    {
                        if (i < k)
                            return;
                        auto bytes = header(1, k, parity, i, size, id);
                        bytes.append(reinterpret_cast<const char*>(data), count);
                        emit_wire(bytes, false, true);
                    });
    }
    void flush()
    {
        if (stopped_ || pending_.empty())
            return;
        auto messages = std::move(pending_);
        pending_.clear();
        pending_bytes_ = 0;
        emit_group(messages, stats_.current_parity_shards);
    }
    void finalize(const group& value)
    {
        stats_.data_shards_rx += value.k;
        for (unsigned i = 0; i < value.k; ++i)
            if (!value.present[i])
                ++stats_.missing_shards;
        finalized_recovered_ += value.recovered;
        if (!value.decoded)
            ++stats_.expired_groups;
    }
    void retire(std::uint32_t id)
    {
        retired_[id % retired_.size()] = id;
        retired_valid_[id % retired_.size()] = true;
    }
    void change(std::uint8_t parity, change_reason reason, std::uint32_t now)
    {
        if (parity == stats_.current_parity_shards)
            return;
        flush();
        if (stopped_)
            return;
        stats_.current_parity_shards = parity;
        stats_.last_change = reason;
        ++stats_.policy_changes;
        changed_ = true;
        changed_at_ = now;
    }
    std::uint32_t conversation_;
    std::size_t mtu_;
    options options_;
    output emit_;
    delivery deliver_;
    statistics stats_;
    std::vector<std::string> pending_;
    std::uint32_t pending_since_{}, next_group_{};
    std::map<std::uint32_t, group> groups_;
    std::array<std::uint32_t, 4096> retired_{};
    std::array<bool, 4096> retired_valid_{};
    std::size_t retained_bytes_{}, pending_bytes_{};
    std::uint32_t highest_{}, feedback_at_{}, feedback_sequence_{}, peer_sequence_{}, sample_at_{}, changed_at_{},
        minimum_rtt_{};
    std::uint64_t peer_data_{}, peer_missing_{}, peer_recovered_{}, finalized_recovered_{}, last_peer_data_{},
        last_peer_missing_{}, last_transmissions_{}, last_retransmissions_{};
    std::size_t previous_queued_bytes_{};
    unsigned stable_{};
    bool have_highest_{}, feedback_started_{}, have_peer_{}, sampling_{}, changed_{}, stopped_{};
};
}
