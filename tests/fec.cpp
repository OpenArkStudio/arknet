#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <arknet/fec/transport.hpp>
#include "check.hpp"

namespace {

constexpr std::uint32_t conversation = 0x12345678;
constexpr std::size_t mtu = 1200;
using arknet::fec::transport;

std::string message(unsigned id, std::size_t size = 64) {
    std::string result(size, '\0');
    for (std::size_t i = 0; i < size; ++i)
    {
        result[i] = static_cast<char>((i * 47 + id * 13) & 255);
    }
    return result;
}

void put(std::string& bytes, std::size_t offset, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i)
    {
        bytes[offset + i] = static_cast<char>(value >> (i * 8));
    }
}

unsigned kind(std::string_view value) {
    return static_cast<unsigned char>(value[5]);
}

arknet::fec::options fixed(unsigned k = 4, unsigned p = 2) {
    auto result = arknet::fec::options::fixed();
    result.data_shards = static_cast<std::uint8_t>(k);
    result.parity_shards = static_cast<std::uint8_t>(p);
    result.max_parity_shards = 4;
    result.max_redundancy_percent = 100;
    result.group_lifetime_ms = 20;
    return result;
}

struct fec_pair
{
    std::vector<std::string> wire, feedback, received;
    transport sender, receiver;

    explicit fec_pair(const arknet::fec::options& settings = fixed())
        : sender(
              conversation, mtu, settings, [&](auto value) { wire.emplace_back(value); }, [](auto) { return true; })
        , receiver(
              conversation, mtu, settings, [&](auto value) { feedback.emplace_back(value); },
              [&](auto value) {
                  received.emplace_back(value);
                  return true;
              }) {}

    std::vector<std::string> send(unsigned count, std::uint32_t now = 0) {
        std::vector<std::string> original;
        for (unsigned i = 0; i < count; ++i)
        {
            original.push_back(message(i, 64 + i));
            check(sender.send(original.back(), now), "datagram queues");
        }
        return original;
    }
};

std::string feedback(std::uint32_t sequence, std::uint64_t data, std::uint64_t missing, std::uint64_t recovered) {
    std::string value(transport::header_size + 24, '\0');
    value.replace(0, 4, "AFEC");
    value[4] = 1;
    value[5] = 2;
    put(value, 10, 24, 2);
    put(value, 12, conversation, 4);
    put(value, 16, sequence, 4);
    put(value, transport::header_size, data, 8);
    put(value, transport::header_size + 8, missing, 8);
    put(value, transport::header_size + 16, recovered, 8);
    return value;
}
} // namespace

DOCTEST_TEST_CASE("Rejected FEC metadata cannot advance the receive replay window") {
    fec_pair value(fixed());
    const auto expected = value.send(1);
    value.sender.update(2);
    auto invalid = value.wire.front();
    invalid[6] = 2;
    put(invalid, 16, 100000, 4);
    check(!value.receiver.input(invalid, 0), "zero-parity multi-data group is invalid");
    check(value.receiver.input(value.wire.front(), 0), "rejected future group did not poison replay state");
    check(value.received == expected, "legitimate original still arrives");
}

DOCTEST_TEST_CASE("Adaptive FEC compares segment and datagram loss ratios independently") {
    auto settings = arknet::fec::options::adaptive();
    transport sender(conversation, mtu, settings, [](auto) {}, [](auto) { return true; });
    sender.adapt(0, 0, 0, 10, 0, 4096);
    for (unsigned i = 0; i < 100; ++i)
    {
        check(sender.send(message(i), 0), "systematic data sends without parity");
    }
    check(sender.input(feedback(1, 100, 10, 0), 1000), "valid peer datagram loss feedback accepted");
    sender.adapt(1000, 1000, 10, 10, 0, 4096);
    check(sender.snapshot().current_parity_shards > 0,
        "10 percent datagram loss activates parity despite 1 percent segment retries");
}

DOCTEST_TEST_CASE("FEC supports maximum shard counts and wire MTU with mixed payload sizes") {
    auto settings = fixed(32, 16);
    settings.max_parity_shards = 16;
    settings.max_buffer_bytes = 8 * 1024 * 1024;
    constexpr std::size_t wire_mtu = 65507;
    std::vector<std::string> wire, received, original;
    transport sender(
        conversation, wire_mtu, settings, [&](auto value) { wire.emplace_back(value); }, [](auto) { return true; });
    transport receiver(
        conversation, wire_mtu, settings, [](auto) {},
        [&](auto value) {
            received.emplace_back(value);
            return true;
        });
    for (unsigned i = 0; i < 32; ++i)
    {
        original.push_back(message(i, wire_mtu - transport::overhead - i * 19));
        check(sender.send(original.back(), 0), "maximum sized datagram queues");
    }
    check(wire.size() == 48, "maximum requested parity emitted within byte budget");
    for (std::size_t i = 16; i < wire.size(); ++i)
    {
        check(wire[i].size() <= wire_mtu, "all headers and parity fit maximum wire MTU");
        check(receiver.input(wire[i], 0), "any 32 surviving shards reconstruct the group");
        check(receiver.snapshot().retained_bytes <= settings.max_buffer_bytes, "retained allocation stays bounded");
    }
    const std::vector<std::string> expected = [&] {
        std::vector<std::string> result(original.begin() + 16, original.end());
        result.insert(result.end(), original.begin(), original.begin() + 16);
        return result;
    }();
    check(received == expected, "sixteen erased maximum-size originals restored exactly");
    check(receiver.snapshot().recovered_shards == 16, "maximum recovery count recorded");
    check(receiver.snapshot().retained_bytes == 0, "complete boundary group releases its cache");
}

DOCTEST_TEST_CASE("FEC CRC32 matches the published check vector") {
    check(arknet::fec::detail::checksum("123456789") == 0xcbf43926U, "standard IEEE CRC32 check vector");
    check(arknet::fec::detail::checksum("") == 0, "empty CRC32");
    check(arknet::fec::detail::checksum(std::string("a\0b", 3)) != arknet::fec::detail::checksum("a"),
        "CRC includes binary zero bytes");
}

DOCTEST_TEST_CASE("FEC options reject unbounded and inconsistent configurations") {
    arknet::fec::options{}.validate();
    arknet::fec::options::fixed().validate();
    arknet::fec::options::adaptive().validate();
    const auto rejects = [](auto mutate) {
        auto settings = arknet::fec::options::fixed();
        mutate(settings);
        DOCTEST_CHECK_THROWS_AS(settings.validate(), std::invalid_argument);
    };
    rejects([](auto& s) { s.mode = static_cast<arknet::fec::policy>(255); });
    rejects([](auto& s) { s.data_shards = 0; });
    rejects([](auto& s) { s.data_shards = 33; });
    rejects([](auto& s) { s.max_parity_shards = 0; });
    rejects([](auto& s) { s.max_parity_shards = 17; });
    rejects([](auto& s) { s.parity_shards = s.max_parity_shards + 1; });
    rejects([](auto& s) { s.min_data_shards = 0; });
    rejects([](auto& s) { s.min_data_shards = s.data_shards + 1; });
    rejects([](auto& s) { s.group_wait_ms = 0; });
    rejects([](auto& s) { s.group_wait_ms = 1001; });
    rejects([](auto& s) { s.group_lifetime_ms = s.group_wait_ms - 1; });
    rejects([](auto& s) { s.group_lifetime_ms = 60001; });
    rejects([](auto& s) { s.max_groups = 0; });
    rejects([](auto& s) { s.max_groups = 1025; });
    rejects([](auto& s) { s.max_buffer_bytes = 4095; });
    rejects([](auto& s) { s.max_buffer_bytes = 64 * 1024 * 1024 + 1; });
    rejects([](auto& s) { s.max_redundancy_percent = 101; });
    rejects([](auto& s) { s.sample_window_ms = 0; });
    rejects([](auto& s) { s.sample_window_ms = 60001; });
    rejects([](auto& s) { s.cooldown_ms = 60001; });
    rejects([](auto& s) { s.min_samples = 0; });
    rejects([](auto& s) { s.enable_loss_percent = 0; });
    rejects([](auto& s) { s.enable_loss_percent = 101; });
    rejects([](auto& s) { s.disable_loss_percent = s.enable_loss_percent; });
    rejects([](auto& s) { s.stable_windows = 0; });
    rejects([](auto& s) { s.overload_queue_percent = 0; });
    rejects([](auto& s) { s.overload_queue_percent = 101; });
    const auto discard = [](auto) {};
    const auto accept = [](auto) { return true; };
    DOCTEST_CHECK_THROWS_AS(
        transport(conversation, transport::overhead + 24, {}, discard, accept), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(transport(conversation, 65508, {}, discard, accept), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(transport(conversation, mtu, {}, {}, accept), std::invalid_argument);
    DOCTEST_CHECK_THROWS_AS(transport(conversation, mtu, {}, discard, {}), std::invalid_argument);
}

DOCTEST_TEST_CASE("FEC immediately delivers systematic datagrams and suppresses duplicate shards") {
    fec_pair connection;
    const auto original = connection.send(4);
    check(connection.wire.size() == 6, "four systematic and two parity shards");
    for (std::size_t i = 0; i < original.size(); ++i)
    {
        check(kind(connection.wire[i]) == 0, "systematic frame type");
        check(connection.receiver.input(connection.wire[i], 0), "systematic shard accepted");
        check(connection.received.size() == i + 1 && connection.received.back() == original[i],
            "no decode wait for intact data");
        check(!connection.receiver.input(connection.wire[i], 0), "same shard rejected as duplicate");
    }
    for (std::size_t i = 4; i < connection.wire.size(); ++i)
    {
        connection.receiver.input(connection.wire[i], 0);
    }
    check(connection.received == original, "parity never repeats delivered datagrams");
    check(connection.receiver.snapshot().recovered_shards == 0, "intact traffic requires no recovery");
    connection.receiver.update(20);
    const auto stats = connection.receiver.snapshot();
    check(stats.data_shards_rx == 4 && stats.missing_shards == 0, "loss observation finalizes once");
    check(stats.retained_groups == 0 && stats.retained_bytes == 0, "expired observation releases buffers");
}

DOCTEST_TEST_CASE("FEC repairs every pair of lost shards despite reversal and duplication") {
    for (unsigned survivors = 0; survivors < 64; ++survivors)
    {
        if (std::popcount(survivors) != 4)
        {
            continue;
        }
        DOCTEST_CAPTURE(survivors);
        fec_pair connection;
        auto original = connection.send(4);
        unsigned lost_data = 0;
        for (int index = 5; index >= 0; --index)
        {
            if (!(survivors & (1U << index)))
            {
                lost_data += index < 4;
                continue;
            }
            check(connection.receiver.input(connection.wire[static_cast<std::size_t>(index)], 0),
                "selected shard accepted");
            check(!connection.receiver.input(connection.wire[static_cast<std::size_t>(index)], 1),
                "selected duplicate rejected");
        }
        auto received = connection.received;
        std::sort(original.begin(), original.end());
        std::sort(received.begin(), received.end());
        check(received == original, "recoverable loss produces all exact datagrams once");
        connection.receiver.update(20);
        const auto stats = connection.receiver.snapshot();
        check(stats.recovered_shards == lost_data && stats.missing_shards == lost_data,
            "recovery and physical data loss are counted separately");
        check(stats.expired_groups == 0, "completed groups are not reported as failed recovery");
    }
}

DOCTEST_TEST_CASE("FEC leaves unrecoverable loss for ARQ and expires incomplete groups") {
    fec_pair connection;
    const auto original = connection.send(4);
    check(connection.receiver.input(connection.wire[1], 0), "one original survives");
    check(connection.receiver.input(connection.wire[4], 0), "first parity survives");
    check(connection.receiver.input(connection.wire[5], 0), "second parity survives");
    check(connection.received == std::vector<std::string>{original[1]},
        "fewer than k shards cannot fabricate missing data");
    connection.receiver.update(20);
    const auto stats = connection.receiver.snapshot();
    check(stats.expired_groups == 1 && stats.missing_shards == 3 && stats.recovered_shards == 0,
        "incomplete group reports loss");
    check(stats.retained_groups == 0 && stats.retained_bytes == 0, "incomplete group releases memory");
    check(!connection.receiver.input(connection.wire[0], 21), "late shard of retired group is ignored");
}

DOCTEST_TEST_CASE("FEC rejects corrupted repair data and permits intact originals to reach ARQ") {
    fec_pair connection;
    const auto original = connection.send(4);
    check(connection.receiver.input(connection.wire[2], 0), "first intact data shard accepted");
    check(connection.receiver.input(connection.wire[3], 0), "second intact data shard accepted");
    auto corrupt = connection.wire[4];
    corrupt[transport::header_size + transport::payload_prefix] ^= 1;
    check(connection.receiver.input(corrupt, 0), "parity corruption is detected when recovery becomes possible");
    DOCTEST_CHECK_NOTHROW(connection.receiver.input(connection.wire[5], 0));
    check((connection.received == std::vector<std::string>{original[2], original[3]}),
        "corrupt recovery never delivers an unverified datagram");
    check(connection.receiver.snapshot().invalid_packets > 0 && connection.receiver.snapshot().retained_bytes == 0,
        "failed decode is observable and releases group state");
    check(connection.receiver.input(connection.wire[0], 1), "intact late original still reaches ARQ");
    check(connection.receiver.input(connection.wire[1], 1), "second intact late original still reaches ARQ");
    auto delivered = connection.received;
    auto expected = original;
    std::sort(delivered.begin(), delivered.end());
    std::sort(expected.begin(), expected.end());
    check(delivered == expected, "intact originals restore all content without corrupt output");
}

DOCTEST_TEST_CASE("FEC bounds group wait and bypasses control datagrams") {
    fec_pair connection;
    const auto original = connection.send(2);
    check(connection.wire.empty(), "partial data group waits briefly");
    const auto ack = message(10, 24);
    check(connection.sender.send(ack, 0, true), "control datagram queues");
    check(connection.wire.size() == 1 && kind(connection.wire.front()) == 3, "ACK-only output bypasses grouping");
    check(connection.receiver.input(connection.wire.front(), 0), "control frame accepted");
    check(connection.received == std::vector<std::string>{ack}, "control frame delivered immediately");
    connection.sender.update(1);
    check(connection.wire.size() == 2 && kind(connection.wire.back()) == 2,
        "only feedback is added before group deadline");
    connection.sender.update(2);
    std::vector<std::string> payload_frames;
    for (const auto& bytes : connection.wire)
    {
        if (kind(bytes) == 0 || kind(bytes) == 1)
        {
            payload_frames.push_back(bytes);
        }
    }
    check(payload_frames.size() == 3, "deadline flushes two data plus one budget-safe parity frame");
    for (const auto& bytes : payload_frames)
    {
        connection.receiver.input(bytes, 2);
    }
    check(connection.received.size() == 3 && connection.received[1] == original[0] &&
              connection.received[2] == original[1],
        "partial group preserves original content");
}

DOCTEST_TEST_CASE("FEC emits independently valid frames when redundancy budget disables parity") {
    auto settings = fixed();
    settings.max_redundancy_percent = 0;
    fec_pair connection(settings);
    const auto original = connection.send(4);
    check(connection.wire.size() == 4, "zero budget emits only data");
    for (const auto& bytes : connection.wire)
    {
        check(connection.receiver.input(bytes, 0), "each data-only frame is valid");
    }
    check(connection.received == original, "budget reduction never loses accepted data");
    check(connection.sender.snapshot().parity_bytes_tx == 0, "no parity overhead exceeds budget");
}

DOCTEST_TEST_CASE("FEC accepts exact MTU boundaries and rejects invalid ARQ datagram sizes") {
    fec_pair connection;
    check(!connection.sender.send({}, 0), "empty input rejected");
    check(!connection.sender.send(message(0, 23), 0), "shorter than ARQ header rejected");
    check(!connection.sender.send(message(0, mtu - transport::overhead + 1), 0), "oversized input rejected");
    const auto bytes = message(0, mtu - transport::overhead);
    check(connection.sender.send(bytes, 0, true), "largest wire-safe datagram accepted");
    check(connection.wire.size() == 1 && connection.wire[0].size() == mtu, "exact maximum wire MTU");
    check(connection.receiver.input(connection.wire[0], 0), "maximum-size frame accepted");
    check(connection.received == std::vector<std::string>{bytes}, "maximum-size binary bytes retained");
}

DOCTEST_TEST_CASE("FEC handles deadline and group-id wrap without duplicate delivery") {
    fec_pair connection;
    const auto start = std::numeric_limits<std::uint32_t>::max() - 1;
    const auto original = connection.send(2, start);
    connection.sender.update(start + 1);
    check(connection.sender.snapshot().data_packets_tx == 0, "one millisecond remains before wrap");
    connection.sender.update(0);
    check(connection.sender.snapshot().data_packets_tx == 2, "wrapped deadline flushes on time");
    for (const auto& value : connection.wire)
    {
        if (kind(value) == 0 || kind(value) == 1)
        {
            connection.receiver.input(value, start);
        }
    }
    check(connection.received == original, "wrapped-time group delivers all data once");
    connection.receiver.update(18);
    check(connection.receiver.snapshot().retained_groups == 0, "wrapped-time group expires on time");
    fec_pair ungrouped(arknet::fec::options{});
    check(ungrouped.sender.send(message(7), 0), "ungrouped datagram queued");
    const auto frame = ungrouped.wire[0];
    for (std::uint32_t id : {0xfffffffeU, 0xffffffffU, 0U, 1U})
    {
        auto wrapped = frame;
        put(wrapped, 16, id, 4);
        check(ungrouped.receiver.input(wrapped, 0), "group sequence crossing zero is accepted");
        check(!ungrouped.receiver.input(wrapped, 1), "wrapped sequence duplicate suppressed");
    }
    check(ungrouped.received.size() == 4, "exactly one delivery per wrapped sequence");
}

DOCTEST_TEST_CASE("FEC applies receive group and aggregate byte limits") {
    auto settings = fixed();
    settings.max_groups = 1;
    settings.max_buffer_bytes = 4096;
    fec_pair connection(settings);
    connection.send(8);
    check(connection.receiver.input(connection.wire[0], 0), "first group retained");
    connection.receiver.input(connection.wire[6], 1);
    check(connection.receiver.snapshot().retained_groups <= 1, "receive group count never exceeds configured bound");
    check(connection.receiver.snapshot().retained_bytes <= settings.max_buffer_bytes, "receive bytes bounded");
    check(connection.receiver.snapshot().evicted_groups >= 1, "admission loss is observable");
    for (std::size_t index : {std::size_t(7), std::size_t(8), std::size_t(9)})
    {
        check(connection.receiver.input(connection.wire[index], 1),
            "group saturation still forwards every intact original");
    }
    check(connection.received.size() == 5, "lack of recovery budget does not drop intact data");
    auto aggregate = fixed(32, 2);
    aggregate.max_buffer_bytes = 4096;
    fec_pair bounded(aggregate);
    for (unsigned i = 0; i < 31; ++i)
    {
        bounded.sender.send(message(i, 900), i);
        check(bounded.sender.snapshot().retained_bytes <= aggregate.max_buffer_bytes,
            "pending transmit data is also bounded");
    }
    fec_pair incoming;
    incoming.send(4);
    bounded.sender.input(incoming.wire.front(), 32);
    check(bounded.sender.snapshot().retained_bytes <= aggregate.max_buffer_bytes,
        "pending transmission and receive groups share aggregate budget");
    bounded.sender.stop();
    check(bounded.sender.snapshot().retained_bytes == 0 && bounded.sender.snapshot().retained_groups == 0,
        "stop releases aggregate state");
}

DOCTEST_TEST_CASE("FEC feedback is valid before and after recovery groups expire") {
    fec_pair connection;
    connection.send(4);
    for (std::size_t index : {std::size_t(2), std::size_t(3), std::size_t(4), std::size_t(5)})
    {
        check(connection.receiver.input(connection.wire[index], 0), "loss pattern accepted");
    }
    connection.receiver.update(0);
    check(connection.feedback.size() == 1 && connection.sender.input(connection.feedback.back(), 0),
        "early recovery feedback has coherent counters");
    connection.receiver.update(1000);
    check(connection.feedback.size() == 2 && connection.sender.input(connection.feedback.back(), 1000),
        "finalized recovery feedback has coherent counters");
    check(!connection.sender.input(connection.feedback.front(), 1001), "stale feedback ignored");
    check(!connection.sender.input(feedback(3, 4, 2, 1), 1002), "decreasing recovery counter rejected");
    check(!connection.sender.input(feedback(3, 3, 2, 2), 1002), "decreasing sample counter rejected");
    check(!connection.sender.input(feedback(3, 4, 5, 2), 1002), "missing cannot exceed total samples");
    check(!connection.sender.input(feedback(3, 4, 2, 3), 1002), "recovery cannot exceed missing samples");
}

DOCTEST_TEST_CASE("Adaptive FEC uses sample minimum, cooldown, hysteresis, and overload protection") {
    auto settings = arknet::fec::options::adaptive();
    settings.sample_window_ms = 10;
    settings.cooldown_ms = 20;
    settings.min_samples = 10;
    settings.stable_windows = 2;
    fec_pair connection(settings);
    auto& controller = connection.sender;
    controller.adapt(0, 0, 0, 20, 0, 100);
    controller.adapt(10, 5, 2, 20, 0, 100);
    check(controller.snapshot().current_parity_shards == 0, "small samples cannot enable FEC");
    controller.adapt(20, 105, 12, 20, 0, 100);
    check(controller.snapshot().current_parity_shards == 2 &&
              controller.snapshot().last_change == arknet::fec::change_reason::loss,
        "ten percent loss enables two repair shards");
    controller.adapt(30, 205, 42, 20, 0, 100);
    check(controller.snapshot().current_parity_shards == 2, "cooldown prevents immediate parameter increase");
    controller.adapt(40, 305, 62, 20, 0, 100);
    check(controller.snapshot().current_parity_shards == 4, "sustained loss reaches configured parity limit");
    controller.adapt(50, 405, 62, 20, 0, 100);
    controller.adapt(60, 505, 62, 20, 0, 100);
    check(controller.snapshot().current_parity_shards == 4, "one eligible healthy window does not reduce parity");
    controller.adapt(70, 605, 62, 20, 0, 100);
    check(controller.snapshot().current_parity_shards == 3 &&
              controller.snapshot().last_change == arknet::fec::change_reason::healthy,
        "stable healthy windows reduce redundancy gradually");
    controller.adapt(80, 705, 62, 20, 0, 99);
    check(controller.snapshot().current_parity_shards == 3,
        "small byte queue limits do not round overload threshold to zero");
    controller.adapt(90, 805, 62, 20, 76, 100);
    check(controller.snapshot().current_parity_shards == 0 &&
              controller.snapshot().last_change == arknet::fec::change_reason::overload,
        "queue overload removes repair traffic");
    controller.adapt(100, 1, 0, 20, 0, 100);
    check(controller.snapshot().current_parity_shards == 0, "counter reset cannot underflow into fabricated loss");
}

DOCTEST_TEST_CASE("Adaptive FEC suppresses repair traffic when RTT and queued bytes rise together") {
    auto settings = arknet::fec::options::adaptive();
    settings.sample_window_ms = 10;
    settings.cooldown_ms = 0;
    settings.min_samples = 10;
    fec_pair connection(settings);
    connection.sender.adapt(0, 0, 0, 20, 0, 1000);
    connection.sender.adapt(10, 100, 10, 20, 0, 1000);
    check(connection.sender.snapshot().current_parity_shards > 0, "loss enables repair traffic");
    connection.sender.adapt(20, 200, 20, 60, 10, 1000);
    check(connection.sender.snapshot().current_parity_shards == 0,
        "RTT growth with growing queue triggers overload guard");
}

DOCTEST_TEST_CASE("FEC tolerates stop called synchronously from output or delivery callbacks") {
    auto settings = fixed();
    std::unique_ptr<transport> emitting;
    unsigned output_count = 0;
    emitting = std::make_unique<transport>(
        conversation, mtu, settings,
        [&](auto) {
            ++output_count;
            emitting->stop();
        },
        [](auto) { return true; });
    for (unsigned i = 0; i < 4; ++i)
    {
        emitting->send(message(i), 0);
    }
    check(output_count == 1, "stop aborts remaining encode callbacks");
    check(emitting->snapshot().retained_bytes == 0, "output stop releases pending memory");
    check(!emitting->send(message(0), 1), "stopped transport rejects future sends");
    emitting->update(1000);
    check(output_count == 1, "stopped transport emits no feedback");
    fec_pair source;
    source.send(4);
    std::unique_ptr<transport> receiving;
    unsigned deliveries = 0;
    receiving = std::make_unique<transport>(
        conversation, mtu, settings, [](auto) {},
        [&](auto) {
            ++deliveries;
            receiving->stop();
            return true;
        });
    DOCTEST_CHECK_NOTHROW(receiving->input(source.wire[0], 0));
    check(deliveries == 1 && receiving->snapshot().retained_bytes == 0,
        "systematic delivery stop leaves no live group reference");
    for (const auto& bytes : source.wire)
    {
        check(!receiving->input(bytes, 1), "stopped transport rejects future packets");
    }
    bool stop_on_recovery = false;
    deliveries = 0;
    receiving = std::make_unique<transport>(
        conversation, mtu, settings, [](auto) {},
        [&](auto) {
            ++deliveries;
            if (stop_on_recovery)
            {
                receiving->stop();
            }
            return true;
        });
    receiving->input(source.wire[2], 0);
    receiving->input(source.wire[3], 0);
    stop_on_recovery = true;
    receiving->input(source.wire[4], 0);
    DOCTEST_CHECK_NOTHROW(receiving->input(source.wire[5], 0));
    check(deliveries == 3 && receiving->snapshot().retained_bytes == 0,
        "recovery callback stop aborts remaining deliveries without stale references");
}

DOCTEST_TEST_CASE("FEC validates malformed frames before allocation and survives deterministic mutation") {
    fec_pair source;
    source.send(4);
    fec_pair target;
    const auto valid = source.wire.front();
    for (std::size_t length = 0; length < valid.size(); ++length)
    {
        check(!target.receiver.input(std::string_view(valid).substr(0, length), 0),
            "every truncated systematic frame rejected");
    }
    for (const auto& [offset, value] : std::vector<std::pair<std::size_t, unsigned>>{
             {0, 'X'}, {4, 2}, {5, 4}, {6, 0}, {6, 33}, {7, 17}, {8, 6}, {9, 1}, {12, 0}, {20, 0}, {22, 0}})
    {
        auto malformed = valid;
        malformed[offset] = static_cast<char>(value);
        check(!target.receiver.input(malformed, 0), "invalid header, length, or CRC rejected");
    }
    check(target.received.empty() && target.receiver.snapshot().retained_bytes == 0,
        "invalid traffic creates no receive state");
    std::uint32_t random = 0x7a4219U;
    const auto next = [&] {
        random = random * 1664525U + 1013904223U;
        return random;
    };
    for (unsigned iteration = 0; iteration < 4000; ++iteration)
    {
        std::string bytes(next() % 1400, '\0');
        for (auto& byte : bytes)
        {
            byte = static_cast<char>(next() >> 24);
        }
        DOCTEST_CHECK_NOTHROW(target.receiver.input(bytes, iteration));
        check(target.receiver.snapshot().retained_bytes <= fixed().max_buffer_bytes,
            "arbitrary bytes cannot exceed memory bound");
    }
    for (unsigned iteration = 0; iteration < 1000; ++iteration)
    {
        auto bytes = source.wire[next() % source.wire.size()];
        bytes[next() % bytes.size()] ^= static_cast<char>(1U << (next() % 8));
        DOCTEST_CHECK_NOTHROW(target.receiver.input(bytes, 4000 + iteration));
        check(target.receiver.snapshot().retained_bytes <= fixed().max_buffer_bytes,
            "near-valid mutation preserves memory bound");
    }
    target.receiver.update(10000);
    check(target.receiver.snapshot().retained_groups == 0 && target.receiver.snapshot().retained_bytes == 0,
        "fuzz-created valid observation state expires");
}
