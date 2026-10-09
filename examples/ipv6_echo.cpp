// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#include <arknet/arknet.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

int main()
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    if (!server.start("::1", 0, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::promise<std::string> reply;
    auto received = reply.get_future();
    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    bool received_once = false;
    client.bind_recv(
        [&](std::string_view data)
        {
            if (!received_once)
            {
                received_once = true;
                reply.set_value(std::string(data));
            }
        });
    if (!client.start("::1", server.get_listen_port(), arknet::use_dgram) || !client.async_send("hello IPv6"))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 2;
    }
    const bool ready = received.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    const bool correct = ready && received.get() == "hello IPv6";
    client.stop();
    server.stop();
    std::cout << (correct ? "IPv6 echo received\n" : "IPv6 echo failed\n");
    return correct ? 0 : 3;
}
