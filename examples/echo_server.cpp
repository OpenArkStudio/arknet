#include <arknet/arknet.hpp>
#include <iostream>

int main(int argc, char** argv)
{
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::string port = argc > 2 ? argv[2] : "7000";
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data) { session->async_send(data); });
    if (!server.start(host, port))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "Listening on " << server.get_listen_address() << ':' << server.get_listen_port() << '\n';
    std::cin.get();
    server.stop();
}
