#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arknet/arq/core.hpp>
#include "check.hpp"

namespace
{
constexpr std::uint32_t conversation = 0x12345678;

void put32(std::string& data, std::size_t offset, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index)
        data[offset + index] = static_cast<char>((value >> (8 * index)) & 0xff);
}

std::uint32_t get32(std::string_view data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + index])) << (8 * index);
    return value;
}

std::string packet(std::uint8_t command, std::uint32_t sequence = 0, std::string_view payload = {},
                   std::uint16_t window = 128, std::uint8_t fragment = 0, std::uint32_t timestamp = 0,
                   std::uint32_t unacknowledged = 0)
{
    std::string data(24, '\0');
    put32(data, 0, conversation);
    data[4] = static_cast<char>(command);
    data[5] = static_cast<char>(fragment);
    data[6] = static_cast<char>(window & 0xff);
    data[7] = static_cast<char>(window >> 8);
    put32(data, 8, timestamp);
    put32(data, 12, sequence);
    put32(data, 16, unacknowledged);
    put32(data, 20, static_cast<std::uint32_t>(payload.size()));
    data.append(payload);
    return data;
}

arknet::arq::options uncongested(std::size_t mtu = 1200)
{
    arknet::arq::options settings;
    settings.mtu = mtu;
    settings.congestion_control = false;
    return settings;
}

struct simulated_link
{
    struct delivery
    {
        std::uint32_t due;
        bool to_right;
        std::string data;
    };
    arknet::arq::control_block left;
    arknet::arq::control_block right;
    std::vector<delivery> pending;
    std::uint32_t now = 0;
    std::size_t transmissions = 0;
    std::size_t dropped = 0;
    std::size_t duplicated = 0;
    bool impair;
    explicit simulated_link(const arknet::arq::options& settings = {}, bool impairments = false)
        : left(conversation, settings), right(conversation, settings), impair(impairments)
    {
        left.set_output([&](auto data) { transmit(true, data); });
        right.set_output([&](auto data) { transmit(false, data); });
    }
    void transmit(bool to_right, std::string_view data)
    {
        ++transmissions;
        if (impair && transmissions % 7 == 0)
        {
            ++dropped;
            return;
        }
        const auto delay = impair ? static_cast<std::uint32_t>((transmissions % 5) * 5 + 5) : 5;
        pending.push_back({now + delay, to_right, std::string(data)});
        if (impair && transmissions % 11 == 0)
        {
            ++duplicated;
            pending.push_back({now + delay + 15, to_right, std::string(data)});
        }
    }
    void step()
    {
        left.update(now);
        right.update(now);
        // Reverse delivery order deliberately; delay values reorder adjacent frames as well.
        for (std::size_t index = pending.size(); index != 0; --index)
        {
            if (pending[index - 1].due > now)
                continue;
            auto delivery = std::move(pending[index - 1]);
            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index - 1));
            check((delivery.to_right ? right : left).input(delivery.data) == 0, "simulated KCP input succeeds");
        }
        now += 5;
    }
};
}

DOCTEST_TEST_CASE("Duplicate ACK bursts cannot exhaust ARQ retries before a timeout")
{
    auto settings = uncongested(60);
    arknet::arq::control_block sender(conversation, settings);
    sender.set_output([](std::string_view) {});
    for (unsigned i = 0; i < 128; ++i)
        check(sender.send(std::string(16, 'x')) == 0, "message queues");
    sender.update(0);
    for (unsigned i = 0; i < 128 && !sender.failed(); ++i)
    {
        check(sender.input(packet(82, 127, {}, 512)) == 0, "late duplicate ACK is valid");
        sender.flush();
    }
    check(!sender.failed(), "ACK-driven resends cannot consume the whole retry budget at time zero");
    check(sender.transmissions() <= 128 * 6, "fast retransmission is bounded independently of timeout retries");
    const auto before = sender.retransmissions();
    sender.update(1000);
    check(sender.retransmissions() > before, "timeout retransmission remains available");
}

DOCTEST_TEST_CASE("ARQ encodes the standard little endian PUSH header")
{
    arknet::arq::control_block sender(conversation, uncongested());
    std::vector<std::string> output;
    sender.set_output([&](auto value) { output.emplace_back(value); });
    check(sender.send(std::string("a\0b", 3)) == 0, "binary message queues");
    check(sender.pending_segments() == 1 && sender.pending_bytes() == 3, "pending accounting");
    sender.update(42);
    check(output.size() == 1, "one datagram");
    check(output.front() == packet(81, 0, std::string("a\0b", 3), 512, 0, 42), "standard KCP frame layout");
    check(sender.conversation() == conversation, "conversation preserved");
    check(sender.input(packet(82, 0, {}, 512, 0, 42, 1)) == 0, "standard ACK decodes");
    check(sender.pending_segments() == 0 && sender.pending_bytes() == 0, "ACK releases payload storage");
}

DOCTEST_TEST_CASE("ARQ fragments and reassembles empty binary and maximum messages")
{
    auto settings = uncongested(80);
    simulated_link link(settings);
    const std::vector<std::string> expected{"", std::string("a\0b", 3), std::string(56 * 256, 'z'), "last"};
    for (const auto& message : expected)
        check(link.left.send(message) == 0, "message queues");
    std::vector<std::string> actual;
    for (int step = 0; step < 2000 && (actual.size() < expected.size() || link.left.pending_segments() != 0); ++step)
    {
        link.step();
        std::string message;
        while (link.right.receive(message))
            actual.push_back(message);
    }
    check(actual == expected, "fragment boundaries and message ordering survive");
    check(link.left.pending_segments() == 0 && !link.left.failed(), "all messages acknowledged");
    for (const auto& delivery : link.pending)
        check(delivery.data.size() <= settings.mtu, "encoded datagrams fit configured MTU");
    check(link.left.send(std::string(56 * 256 + 1, 'x')) == -1, "fragment field overflow rejected");
}

DOCTEST_TEST_CASE("ARQ reliable delivery survives deterministic loss duplication and reordering")
{
    for (const bool congestion_control : {false, true})
    {
        DOCTEST_INFO("congestion_control=", congestion_control);
        auto settings = uncongested(300);
        settings.congestion_control = congestion_control;
        settings.max_retransmits = 40;
        simulated_link link(settings, true);
        std::vector<std::string> expected_left;
        std::vector<std::string> expected_right;
        for (int index = 0; index < 80; ++index)
        {
            const auto message = std::to_string(index) + std::string(static_cast<std::size_t>(index * 17), 'x');
            expected_right.push_back(message);
            expected_left.push_back("reply:" + message);
            check(link.left.send(expected_right.back()) == 0, "left message queues");
            check(link.right.send(expected_left.back()) == 0, "right message queues");
        }
        std::vector<std::string> received_left;
        std::vector<std::string> received_right;
        for (int step = 0; step < 20000; ++step)
        {
            link.step();
            std::string message;
            while (link.left.receive(message))
                received_left.push_back(message);
            while (link.right.receive(message))
                received_right.push_back(message);
            if (received_left.size() == expected_left.size() && received_right.size() == expected_right.size() &&
                link.left.pending_segments() == 0 && link.right.pending_segments() == 0)
                break;
        }
        check(link.dropped != 0 && link.duplicated != 0, "link actually exercised impairments");
        check(received_left == expected_left && received_right == expected_right,
              "both directions deliver each complete message exactly once in order");
        check(link.left.pending_segments() == 0 && link.right.pending_segments() == 0, "all segments acknowledged");
        check(!link.left.failed() && !link.right.failed(), "recoverable loss does not fail the connection");
    }
}

DOCTEST_TEST_CASE("ARQ rejects malformed datagrams before changing receive state")
{
    const auto valid = packet(81, 0, "good");
    std::vector<std::string> invalid{"", "short", valid.substr(0, 23), valid.substr(0, valid.size() - 1)};
    auto wrong_conversation = valid;
    put32(wrong_conversation, 0, 99);
    invalid.push_back(wrong_conversation);
    invalid.push_back(packet(80));
    invalid.push_back(packet(85));
    invalid.push_back(packet(82, 0, "control payload"));
    invalid.push_back(packet(83, 0, {}, 128, 1));
    invalid.push_back(packet(81, 0, "future UNA", 128, 0, 0, 1));
    auto enormous_length = valid;
    put32(enormous_length, 20, std::numeric_limits<std::uint32_t>::max());
    invalid.push_back(enormous_length);
    invalid.push_back(valid + "trailing");
    invalid.push_back(valid + packet(90));
    invalid.push_back(std::string(65508, 'x'));
    for (const auto& datagram : invalid)
    {
        arknet::arq::control_block receiver(conversation);
        check(receiver.input(datagram) < 0, "malformed packet rejected");
        std::string message = "unchanged";
        check(!receiver.receive(message) && message == "unchanged", "failed datagram has no partial delivery");
        check(receiver.input(valid) == 0 && receiver.receive(message) && message == "good", "valid input still works");
    }
    auto settings = uncongested();
    settings.receive_window = 2;
    arknet::arq::control_block receiver(conversation, settings);
    check(receiver.input(packet(81, 0, "bad fragment", 128, 2)) < 0, "impossible fragment count rejected");
}

DOCTEST_TEST_CASE("ARQ checks fragment countdown before delivering a message")
{
    arknet::arq::control_block receiver(conversation);
    check(receiver.input(packet(81, 0, "first", 128, 1) + packet(81, 1, "second", 128, 1)) == 0,
          "fragments pass frame validation");
    std::string message;
    check(!receiver.receive(message) && receiver.failed(), "inconsistent reassembly fails without corrupt delivery");
}

DOCTEST_TEST_CASE("ARQ future acknowledgements and malformed ACK batches retain pending data")
{
    arknet::arq::control_block sender(conversation, uncongested());
    sender.set_output([](auto) {});
    check(sender.send("unacknowledged") == 0, "message queues");
    sender.update(0);
    check(sender.input(packet(82, 0, {}, 128, 0, 0, 1) + packet(90)) < 0, "malformed ACK batch rejected atomically");
    check(sender.pending_segments() == 1 && sender.pending_bytes() == 14,
          "valid ACK prefix of malformed datagram does not release data");
    check(sender.input(packet(82, 1)) == 0 && sender.pending_segments() == 1,
          "unsent sequence cannot acknowledge data");
    check(sender.input(packet(82, 0, {}, 128, 0, 0, 2)) < 0 && sender.pending_segments() == 1,
          "UNA beyond the next send sequence rejected");
    check(sender.input(packet(82, 0, {}, 128, 0, 0, 1)) == 0 && sender.pending_segments() == 0,
          "valid ACK still releases the original message");
}

DOCTEST_TEST_CASE("ARQ rejects inconsistent partial messages before filling the receive window")
{
    auto settings = uncongested();
    settings.receive_window = 4;
    arknet::arq::control_block receiver(conversation, settings);
    check(receiver.input(packet(81, 0, "first", 128, 3) + packet(81, 1, "unexpected end")) == 0,
          "partial fragments decode");
    std::string message = "unchanged";
    check(!receiver.receive(message) && receiver.failed() && message == "unchanged",
          "contradictory partial fragments fail immediately");
}

DOCTEST_TEST_CASE("ARQ duplicate acknowledgement backlog and receive buffers remain bounded")
{
    auto settings = uncongested(80);
    settings.receive_window = 4;
    arknet::arq::control_block receiver(conversation, settings);
    std::vector<std::string> output;
    receiver.set_output([&](auto data) { output.emplace_back(data); });
    check(receiver.input(packet(81, 0, "first")) == 0, "first message queues");
    for (std::uint32_t index = 1; index < 5000; ++index)
    {
        const auto old_sequence = std::numeric_limits<std::uint32_t>::max() - index;
        check(receiver.input(packet(81, old_sequence, "duplicate")) == 0, "old sequence is acknowledged as duplicate");
        check(receiver.input(packet(81, 4, "last in window")) == 0, "duplicate out of order sequence accepted once");
        check(receiver.input(packet(81, 5, "outside window")) == 0, "sequence outside window ignored");
    }
    receiver.update(10);
    std::size_t acknowledgement_bytes = 0;
    for (const auto& data : output)
        acknowledgement_bytes += data.size();
    check(acknowledgement_bytes <= settings.receive_window * 24, "ACK storage is bounded before a flush");
    check(receiver.input(packet(81, 1, "second") + packet(81, 2, "third") + packet(81, 3, "fourth")) == 0,
          "missing in-window messages accepted");
    std::vector<std::string> actual;
    std::string message;
    while (receiver.receive(message))
        actual.push_back(message);
    check((actual == std::vector<std::string>{"first", "second", "third", "fourth", "last in window"}),
          "duplicates and future-window messages never enter reassembly");
}

DOCTEST_TEST_CASE("ARQ bounds pending messages including empty payloads")
{
    auto settings = uncongested(80);
    settings.max_pending_bytes = 100;
    arknet::arq::control_block sender(conversation, settings);
    check(sender.send(std::string(76, 'x')) == -2, "payload and fragment headers count toward bound");
    check(sender.pending_segments() == 0, "queue rejection is atomic");
    check(sender.send(std::string(56, 'x')) == 0, "bounded message accepted");
    check(sender.send("") == -2 && sender.pending_bytes() == 56, "empty message cannot bypass queue bound");
    arknet::arq::control_block empty_sender(conversation, settings);
    for (int index = 0; index < 4; ++index)
        check(empty_sender.send("") == 0, "bounded empty message accepted");
    check(empty_sender.send("") == -2, "empty message count bounded");
}

DOCTEST_TEST_CASE("ARQ receive wire-byte budget bounds ordered and out of order payloads")
{
    auto settings = uncongested(80);
    settings.max_pending_bytes = 100;
    arknet::arq::control_block receiver(conversation, settings);
    std::vector<std::string> output;
    receiver.set_output([&](auto data) { output.emplace_back(data); });
    check(receiver.input(packet(81, 0, std::string(40, 'a'))) == 0, "first message fits receive budget");
    receiver.update(0);
    output.clear();
    const auto later = packet(81, 2, std::string(20, 'c'));
    check(receiver.input(later) == -5, "aggregate receive wire bytes cannot exceed configured budget");
    receiver.flush();
    check(output.empty(), "receive-budget rejection does not acknowledge the rejected sequence");
    std::string message;
    check(receiver.receive(message) && message == std::string(40, 'a'), "reading releases receive bytes");
    check(receiver.input(later) == 0, "out of order retry fits after byte budget is released");
    check(receiver.input(packet(81, 1, std::string(20, 'b'))) == 0, "ordered and out of order bytes share one budget");
    check(receiver.receive(message) && message == std::string(20, 'b') && receiver.receive(message) &&
              message == std::string(20, 'c'),
          "promotion does not charge buffered bytes twice");
    receiver.flush();
    output.clear();
    check(receiver.input(packet(81, 3, std::string(77, 'x'))) == -5, "single frame larger than byte budget rejected");
    receiver.flush();
    check(output.empty() && !receiver.failed(), "budget rejection emits no ACK and caller may release queued bytes");
    check(receiver.input(packet(81, 3, "small")) == 0 && receiver.receive(message) && message == "small",
          "all receive bytes are released after completed reads");
}

DOCTEST_TEST_CASE("ARQ empty receive messages count headers toward the byte budget")
{
    auto settings = uncongested();
    settings.max_pending_bytes = 24;
    arknet::arq::control_block receiver(conversation, settings);
    check(receiver.input(packet(81, 0)) == 0, "one empty frame fills the wire-byte budget");
    check(receiver.input(packet(81, 1)) == -5, "empty receive messages cannot bypass memory limit");
    std::string message = "previous";
    check(receiver.receive(message) && message.empty(), "empty read releases its header budget");
    check(receiver.input(packet(81, 1)) == 0 && receiver.receive(message) && message.empty(),
          "released empty frame budget can be reused");
}

DOCTEST_TEST_CASE("ARQ retries stop at the configured retransmission limit")
{
    auto settings = uncongested();
    settings.max_retransmits = 2;
    arknet::arq::control_block sender(conversation, settings);
    int transmissions = 0;
    sender.set_output([&](auto) { ++transmissions; });
    check(sender.send("unreachable") == 0, "message queued");
    for (std::uint32_t now = 0; now < 3000; now += 10)
        sender.update(now);
    check(sender.failed() && transmissions == 3, "initial transmission plus two retries then failure");
    check(sender.send("later") == -1, "failed connection rejects new data");
}

DOCTEST_TEST_CASE("ARQ timer deadlines cross the 32 bit timestamp rollover")
{
    arknet::arq::control_block sender(conversation, uncongested());
    std::vector<std::string> output;
    sender.set_output([&](auto data) { output.emplace_back(data); });
    check(sender.send("wrap") == 0, "message queued");
    const auto start = std::numeric_limits<std::uint32_t>::max() - 100;
    sender.update(start);
    sender.update(start + 190);
    check(output.size() == 1, "retry remains pending after timestamp rollover");
    sender.update(start + 200);
    check(output.size() == 2 && get32(output.back(), 8) == start + 200, "timeout fires at wrapped deadline");
    check(sender.input(packet(82, 0, {}, 512, 0, start + 200, 1)) == 0, "ACK timestamp rolls over");
    check(sender.pending_segments() == 0 && !sender.failed(), "wrapped retry acknowledged");
}

DOCTEST_TEST_CASE("ARQ ignores old wrapped sequence numbers and packets beyond receive window")
{
    arknet::arq::control_block receiver(conversation, uncongested());
    check(receiver.input(packet(81, std::numeric_limits<std::uint32_t>::max(), "old")) == 0,
          "old sequence accepted as duplicate");
    check(receiver.input(packet(81, 512, "ahead")) == 0, "out of window packet ignored");
    check(receiver.input(packet(81, 0, "current")) == 0, "current sequence accepted");
    std::string message;
    check(receiver.receive(message) && message == "current" && !receiver.receive(message),
          "old and future packets do not contaminate ordered receive");
}

DOCTEST_TEST_CASE("ARQ fast retransmit precedes the timeout")
{
    auto settings = uncongested(25);
    arknet::arq::control_block sender(conversation, settings);
    std::vector<std::string> output;
    sender.set_output([&](auto data) { output.emplace_back(data); });
    check(sender.send("abcd") == 0, "four fragments queue");
    sender.update(0);
    check(output.size() == 4, "four initial datagrams");
    sender.update(10);
    check(sender.input(packet(82, 1)) == 0 && sender.input(packet(82, 2)) == 0, "later fragments acknowledged");
    sender.flush();
    check(output.size() == 5 && get32(output.back(), 12) == 0, "missing fragment retransmitted before timeout");
}

DOCTEST_TEST_CASE("ARQ full receiver advertises zero window and reopens after reads")
{
    auto settings = uncongested(80);
    settings.receive_window = 2;
    simulated_link link(settings);
    check(link.left.input(packet(84, 0, {}, 2)) == 0, "peer advertises its initial receive window");
    for (const auto* message : {"first", "second", "third", "fourth"})
        check(link.left.send(message) == 0, "message queues");
    for (int index = 0; index < 100; ++index)
        link.step();
    check(link.left.pending_segments() == 2, "sender stops at receiver window");
    std::vector<std::string> actual;
    for (int index = 0; index < 1000; ++index)
    {
        std::string message;
        while (link.right.receive(message))
            actual.push_back(message);
        link.step();
        if (actual.size() == 4 && link.left.pending_segments() == 0)
            break;
    }
    check((actual == std::vector<std::string>{"first", "second", "third", "fourth"}), "reads restore the send window");
    check(link.left.pending_segments() == 0, "remaining messages acknowledged");
}

DOCTEST_TEST_CASE("ARQ probes a zero remote window and answers window requests")
{
    arknet::arq::control_block sender(conversation, uncongested());
    std::vector<std::string> output;
    sender.set_output([&](auto data) { output.emplace_back(data); });
    check(sender.input(packet(84, 0, {}, 0)) == 0, "peer closes receive window");
    check(sender.send("waiting") == 0, "message waits for window");
    sender.update(0);
    sender.update(6990);
    check(output.empty(), "no PUSH while peer window is zero");
    sender.update(7000);
    check(output.size() == 1 && static_cast<unsigned char>(output.back()[4]) == 83, "zero window probe emitted");
    check(sender.input(packet(84)) == 0, "peer announces reopened window");
    sender.update(7010);
    check(output.size() == 2 && static_cast<unsigned char>(output.back()[4]) == 81, "queued data resumes");
    check(sender.input(packet(83)) == 0, "window request accepted");
    sender.flush();
    check(static_cast<unsigned char>(output.back()[4]) == 84, "window response emitted");
}

DOCTEST_TEST_CASE("ARQ absent output retains pending work until output is installed")
{
    arknet::arq::control_block sender(conversation);
    check(sender.send("queued") == 0, "message queued");
    sender.update(0);
    sender.update(1000);
    int calls = 0;
    sender.set_output([&](auto) { ++calls; });
    sender.flush();
    check(calls == 1 && !sender.failed(), "missing transport does not consume retries");
}

DOCTEST_TEST_CASE("ARQ constructor rejects unusable configuration")
{
    for (int index = 0; index < 7; ++index)
    {
        arknet::arq::options settings;
        switch (index)
        {
        case 0:
            settings.mtu = 24;
            break;
        case 1:
            settings.mtu = 65508;
            break;
        case 2:
            settings.send_window = 0;
            break;
        case 3:
            settings.receive_window = 0;
            break;
        case 4:
            settings.interval = 0;
            break;
        case 5:
            settings.max_retransmits = 0;
            break;
        case 6:
            settings.max_pending_bytes = 23;
            break;
        }
        DOCTEST_CHECK_THROWS_AS(arknet::arq::control_block(conversation, settings), std::invalid_argument);
    }
    arknet::arq::options settings;
    settings.interval = 5001;
    DOCTEST_CHECK_THROWS_AS(arknet::arq::control_block(conversation, settings), std::invalid_argument);
}
