#include <arknet/base/detail/keepalive_options.hpp>
#include "check.hpp"
#include <array>
#include <limits>

DOCTEST_TEST_CASE("TCP keepalive validates parameters before changing socket options")
{
    asio::io_context context;
    asio::ip::tcp::socket socket(context);
    check(!arknet::detail::set_keepalive_options(socket), "closed socket rejects keepalive");
    check(arknet::get_last_error() == asio::error::not_connected, "closed socket error");
    socket.open(asio::ip::tcp::v4());
    check(arknet::detail::set_keepalive_options(socket, false), "disable keepalive");
    for (const auto values :
         {std::array<unsigned, 3>{0, 3, 3}, {60, 0, 3}, {60, 3, 0}, {(std::numeric_limits<unsigned>::max)(), 3, 3}})
    {
        check(!arknet::detail::set_keepalive_options(socket, true, values[0], values[1], values[2]),
              "invalid keepalive parameters fail");
        check(arknet::get_last_error() == asio::error::invalid_argument, "invalid parameter error");
        asio::socket_base::keep_alive option;
        socket.get_option(option);
        check(!option.value(), "validation preserves disabled keepalive");
    }
    check(arknet::detail::set_keepalive_options(socket, true, 30, 5, 4), "configure native keepalive");
    asio::socket_base::keep_alive option;
    socket.get_option(option);
    check(option.value() && !arknet::get_last_error(), "keepalive is enabled");
    check(arknet::detail::set_keepalive_options(socket, false, 0, 0, 0), "disable ignores timing parameters");
    socket.get_option(option);
    check(!option.value(), "keepalive is disabled");
}

DOCTEST_TEST_CASE("UDP keepalive is a successful no-op")
{
    asio::io_context context;
    asio::ip::udp::socket socket(context);
    check(arknet::detail::set_keepalive_options(socket), "UDP has no TCP keepalive settings");
    check(!arknet::get_last_error() && !socket.is_open(), "UDP socket remains unchanged");
}
