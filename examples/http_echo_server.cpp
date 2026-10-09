#include <arknet/arknet.hpp>
#include <iostream>

int main(int argc, char** argv)
{
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::string port = argc > 2 ? argv[2] : "8080";
    arknet::http_server server;
    server.bind_recv(
        [](auto& session, arknet::http::request<arknet::http::string_body>& request)
        {
            arknet::http::response<arknet::http::string_body> response(arknet::http::status::ok, request.version());
            response.keep_alive(request.keep_alive());
            response.set(arknet::http::field::content_type, "application/octet-stream");
            response.body() = request.body();
            response.prepare_payload();
            session->async_send(std::move(response));
        });
    if (!server.start(host, port))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "Listening on " << server.get_listen_address() << ':' << server.get_listen_port() << '\n';
    std::cin.get();
    server.stop();
}
