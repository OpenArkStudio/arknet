#include <string>
#include <string_view>
#include <vector>

#include <arknet/fec/transport.hpp>

int installed_fec_selfcheck() {
    auto settings = arknet::fec::options::fixed();
    settings.data_shards = 2;
    settings.parity_shards = 1;
    std::vector<std::string> wire, received;
    arknet::fec::transport sender(
        7, 1200, settings, [&](std::string_view bytes) { wire.emplace_back(bytes); },
        [](std::string_view) { return true; });
    arknet::fec::transport receiver(
        7, 1200, settings, [](std::string_view) {},
        [&](std::string_view bytes) {
            received.emplace_back(bytes);
            return true;
        });
    const std::string first(24, 'a'), second(24, 'b');
    if (!sender.send(first, 0) || !sender.send(second, 0) || wire.size() != 3)
    {
        return 1;
    }
    if (!receiver.input(wire[1], 0) || !receiver.input(wire[2], 0))
    {
        return 2;
    }
    return received == std::vector<std::string>{second, first} ? 0 : 3;
}
