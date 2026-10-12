# 创建 HTTP 服务端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动服务端，再运行[对应客户端](client_CN.md)。

## HTTP 回显服务端

按[构建指南](../../get-started/installation_CN.md)，通过 `arknet::arknet` 接口目标在 C++20 应用中接入网络库。将本页服务端与[客户端](client_CN.md)分别构建为可执行程序。先启动服务端，再在另一个终端运行客户端。在服务端终端按 Enter 停止服务端。普通 HTTP 不需要启用 TLS。

```cpp
#include <iostream>
#include <utility>

#include <arknet/http/http_server.hpp>

int main()
{
    arknet::http_server server;
    server.bind_recv([](auto& session, arknet::http::request<arknet::http::string_body>& request)
    {
        arknet::http::response<arknet::http::string_body> response(
            arknet::http::status::ok, request.version());
        response.keep_alive(request.keep_alive());
        response.set(arknet::http::field::content_type, "text/plain");
        response.body() = request.body();
        response.prepare_payload();
        session->async_send(std::move(response));
    });
    if (!server.start("127.0.0.1", 8080))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "HTTP listening on http://127.0.0.1:8080/echo\n";
    std::cin.get();
    server.stop();
}
```


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
