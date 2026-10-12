# 创建 TCP 服务端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动服务端，再运行[对应客户端](client_CN.md)。

## 回显服务端

通过 `arknet::arknet` 接口目标，将本页服务端与[客户端](client_CN.md)分别构建为 C++20 可执行程序。先启动服务端，再在另一个终端运行客户端。在服务端终端按 Enter 停止服务端。两端均使用 `use_dgram` 长度拆包；客户端在主线程等待回显，不在 IO 回调中等待。

```cpp
#include <arknet/tcp/tcp_server.hpp>
#include <iostream>
#include <string_view>

int main()
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view bytes)
    {
        session->async_send(bytes);
    });
    if (!server.start("127.0.0.1", 7000, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "TCP listening on 127.0.0.1:7000\n";
    std::cin.get();
    server.stop();
}
```


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
