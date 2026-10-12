#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vendor/kcp/ikcp.h"

#include <arknet/arq/core.hpp>
#include "check.hpp"

namespace
{
constexpr std::uint32_t interop_conversation = 0x83a27104;

std::uint32_t read_wire32(std::string_view data, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + index])) << (8 * index);
    return value;
}

void write_wire32(std::string& data, std::size_t offset, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index)
        data[offset + index] = static_cast<char>((value >> (8 * index)) & 0xff);
}

std::string wire_packet(std::uint8_t command, std::string_view payload = {}, std::uint32_t timestamp = 0,
                        std::uint32_t unacknowledged = 0)
{
    std::string data(24, '\0');
    write_wire32(data, 0, interop_conversation);
    data[4] = static_cast<char>(command);
    data[6] = static_cast<char>(128);
    write_wire32(data, 8, timestamp);
    write_wire32(data, 16, unacknowledged);
    write_wire32(data, 20, static_cast<std::uint32_t>(payload.size()));
    data.append(payload);
    return data;
}

struct reference_link
{
    struct delivery
    {
        std::uint32_t due;
        bool to_modern;
        std::string data;
    };
    arknet::arq::control_block modern;
    std::unique_ptr<ikcpcb, decltype(&ikcp_release)> reference;
    std::vector<delivery> pending;
    std::array<std::array<std::size_t, 4>, 2> commands{};
    std::uint32_t now = 0;
    std::size_t transmissions = 0;
    std::size_t losses = 0;
    std::size_t duplicates = 0;
    bool impair;
    bool drop_modern_window = false;
    bool drop_reference_window = false;

    reference_link(const arknet::arq::options& settings, int reference_mtu, bool impairments)
        : modern(interop_conversation, settings), reference(ikcp_create(interop_conversation, this), &ikcp_release),
          impair(impairments)
    {
        check(reference != nullptr, "canonical reference allocates");
        check(ikcp_setmtu(reference.get(), reference_mtu) == 0, "canonical reference MTU configured");
        check(ikcp_wndsize(reference.get(), settings.send_window, settings.receive_window) == 0,
              "canonical reference windows configured");
        check(ikcp_nodelay(reference.get(), settings.nodelay ? 1 : 0, static_cast<int>(settings.interval),
                           static_cast<int>(settings.fast_resend), settings.congestion_control ? 0 : 1) == 0,
              "canonical reference retransmission mode configured");
        reference->dead_link = settings.max_retransmits + 1;
        ikcp_setoutput(reference.get(), reference_output);
        modern.set_output([&](auto data) { transmit(false, data); });
    }

    static int reference_output(const char* data, int count, ikcpcb*, void* user)
    {
        static_cast<reference_link*>(user)->transmit(true, {data, static_cast<std::size_t>(count)});
        return 0;
    }

    void transmit(bool to_modern, std::string_view data)
    {
        ++transmissions;
        bool contains_window = false;
        for (std::size_t offset = 0; offset < data.size();)
        {
            check(data.size() - offset >= 24, "reference output contains complete KCP headers");
            const auto command = static_cast<unsigned char>(data[offset + 4]);
            check(command >= 81 && command <= 84, "reference output contains standard KCP commands");
            ++commands[to_modern ? 1 : 0][command - 81];
            contains_window = contains_window || command == 84;
            const auto length = read_wire32(data, offset + 20);
            check(length <= data.size() - offset - 24, "reference output payload fits frame");
            offset += 24 + length;
        }
        auto& drop_window = to_modern ? drop_reference_window : drop_modern_window;
        if (contains_window && drop_window)
        {
            drop_window = false;
            ++losses;
            return;
        }
        if (impair && transmissions % 7 == 0)
        {
            ++losses;
            return;
        }
        const auto delay = impair ? static_cast<std::uint32_t>(5 + (transmissions % 5) * 5) : 5;
        pending.push_back({now + delay, to_modern, std::string(data)});
        if (impair && transmissions % 11 == 0)
        {
            ++duplicates;
            pending.push_back({now + delay + 15, to_modern, std::string(data)});
        }
    }

    void send(bool from_modern, std::string_view message)
    {
        if (from_modern)
            check(modern.send(message) == 0, "modern peer queues a message");
        else
            check(ikcp_send(reference.get(), message.data(), static_cast<int>(message.size())) >= 0,
                  "canonical peer queues a message");
    }

    bool receive(bool on_modern, std::string& message)
    {
        if (on_modern)
            return modern.receive(message);
        const auto size = ikcp_peeksize(reference.get());
        if (size < 0)
            return false;
        message.resize(static_cast<std::size_t>(size));
        check(ikcp_recv(reference.get(), message.data(), size) == size, "canonical peer receives a complete message");
        return true;
    }

    void step()
    {
        modern.update(now);
        ikcp_update(reference.get(), now);
        // Deliver due datagrams in reverse order to exercise reordered segments.
        for (std::size_t index = pending.size(); index != 0; --index)
        {
            if (pending[index - 1].due > now)
                continue;
            auto value = std::move(pending[index - 1]);
            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index - 1));
            if (value.to_modern)
                check(modern.input(value.data) == 0, "modern peer accepts canonical wire frames");
            else
                check(ikcp_input(reference.get(), value.data.data(), static_cast<long>(value.data.size())) == 0,
                      "canonical peer accepts modern wire frames");
        }
        now += 5;
    }

    bool idle() const { return modern.pending_segments() == 0 && ikcp_waitsnd(reference.get()) == 0; }
};
}

DOCTEST_TEST_CASE("ARQ interoperates bidirectionally with the pinned canonical C implementation")
{
    for (const bool congestion : {false, true})
    {
        for (const auto [modern_mtu, reference_mtu] : {std::pair{600, 600}, std::pair{1200, 1400}})
        {
            DOCTEST_INFO("congestion=", congestion, ", modern MTU=", modern_mtu, ", reference MTU=", reference_mtu);
            arknet::arq::options settings;
            settings.mtu = static_cast<std::size_t>(modern_mtu);
            settings.congestion_control = congestion;
            settings.max_retransmits = 40;
            reference_link link(settings, reference_mtu, true);
            std::vector<std::string> expected_modern;
            std::vector<std::string> expected_reference;
            for (int round = 0; round < 8; ++round)
            {
                for (const std::size_t size : {0, 1, 64, 4119, 16384})
                {
                    expected_modern.emplace_back(size, static_cast<char>('a' + round));
                    expected_reference.emplace_back(size, static_cast<char>('A' + round));
                    if (size > 1)
                    {
                        expected_modern.back()[size / 2] = '\0';
                        expected_reference.back()[size / 2] = '\0';
                    }
                    link.send(false, expected_modern.back());
                    link.send(true, expected_reference.back());
                }
            }
            std::vector<std::string> received_modern;
            std::vector<std::string> received_reference;
            for (int step = 0; step < 30000; ++step)
            {
                link.step();
                std::string message;
                while (link.receive(true, message))
                    received_modern.push_back(message);
                while (link.receive(false, message))
                    received_reference.push_back(message);
                if (link.idle() && received_modern.size() == expected_modern.size() &&
                    received_reference.size() == expected_reference.size())
                    break;
            }
            check(link.losses > 0 && link.duplicates > 0, "interop link exercises loss and duplication");
            check(received_modern == expected_modern && received_reference == expected_reference,
                  "both implementations deliver empty binary and fragmented messages exactly once in order");
            check(link.idle() && !link.modern.failed(), "both implementations release all acknowledged messages");
            check(link.commands[0][1] > 0 && link.commands[1][1] > 0, "both implementations emit ACK frames");
        }
    }
}

DOCTEST_TEST_CASE("ARQ canonical interoperability recovers zero windows and lost window announcements")
{
    for (const bool receiver_is_modern : {false, true})
    {
        DOCTEST_INFO("receiver_is_modern=", receiver_is_modern);
        arknet::arq::options settings;
        settings.receive_window = 128;
        settings.congestion_control = false;
        reference_link link(settings, 1200, false);
        std::vector<std::string> expected;
        for (int index = 0; index < 200; ++index)
        {
            expected.push_back(std::to_string(index));
            link.send(!receiver_is_modern, expected.back());
        }
        while (link.now < 8000)
            link.step();
        const auto sender_index = receiver_is_modern ? 1 : 0;
        const auto receiver_index = receiver_is_modern ? 0 : 1;
        check(!link.idle() && link.commands[sender_index][2] > 0 && link.commands[receiver_index][3] > 0,
              "sender probes the full peer and the receiver reports its zero window");
        if (receiver_is_modern)
            link.drop_modern_window = true;
        else
            link.drop_reference_window = true;
        std::vector<std::string> received;
        while (link.now < 30000)
        {
            std::string message;
            while (link.receive(receiver_is_modern, message))
                received.push_back(message);
            link.step();
            if (received.size() == expected.size() && link.idle())
                break;
        }
        check(link.losses == 1 && received == expected && link.idle(),
              "both implementations resume ordered delivery after a lost WINS frame");
        check(link.commands[sender_index][2] >= 2, "a later window probe recovers the lost announcement");
    }
}

DOCTEST_TEST_CASE("ARQ ACK and WASK responses match canonical wire vectors")
{
    std::vector<std::string> modern_output;
    std::vector<std::string> reference_output;
    arknet::arq::options settings;
    settings.receive_window = 128;
    settings.congestion_control = false;
    arknet::arq::control_block modern(interop_conversation, settings);
    std::unique_ptr<ikcpcb, decltype(&ikcp_release)> reference(ikcp_create(interop_conversation, &reference_output),
                                                               &ikcp_release);
    check(reference != nullptr, "canonical peer allocates");
    ikcp_setoutput(reference.get(),
                   [](const char* data, int size, ikcpcb*, void* user)
                   {
                       static_cast<std::vector<std::string>*>(user)->emplace_back(data, static_cast<std::size_t>(size));
                       return 0;
                   });
    check(ikcp_wndsize(reference.get(), 128, 128) == 0 && ikcp_nodelay(reference.get(), 1, 10, 2, 1) == 0,
          "canonical peer configured");
    modern.set_output([&](auto data) { modern_output.emplace_back(data); });
    const auto push = wire_packet(81, "wire", 37);
    check(modern.input(push) == 0 && ikcp_input(reference.get(), push.data(), static_cast<long>(push.size())) == 0,
          "both implementations accept a standard PUSH vector");
    std::string message;
    check(modern.receive(message) && message == "wire", "modern peer reassembles the vector");
    std::array<char, 4> buffer{};
    check(ikcp_recv(reference.get(), buffer.data(), 4) == 4 && std::string_view(buffer.data(), 4) == "wire",
          "canonical peer reassembles the vector");
    modern.update(42);
    ikcp_update(reference.get(), 42);
    check(modern_output.size() == 1 && modern_output == reference_output &&
              modern_output.front() == wire_packet(82, {}, 37, 1),
          "ACK vectors match byte for byte");
    modern_output.clear();
    reference_output.clear();
    const auto probe = wire_packet(83);
    check(modern.input(probe) == 0 && ikcp_input(reference.get(), probe.data(), static_cast<long>(probe.size())) == 0,
          "both implementations accept a standard WASK vector");
    modern.update(52);
    ikcp_update(reference.get(), 52);
    check(modern_output.size() == 1 && modern_output == reference_output &&
              modern_output.front() == wire_packet(84, {}, 0, 1),
          "WINS vectors match byte for byte");
}
