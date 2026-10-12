# 创建 UDP 服务端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动服务端，再运行[对应客户端](client_CN.md)。

## 回显服务端

服务端通过来源端点对应的 session 回复。通过 `arknet::arknet` 接口目标，将本页服务端与[客户端](client_CN.md)分别构建为可执行程序。先启动服务端，再在另一个终端运行客户端。在服务端终端按 Enter 停止服务端。

```cpp
#include <arknet/udp/udp_server.hpp>
#include <iostream>
#include <string_view>

int main()
{
    arknet::udp_server server;
    server.bind_recv([](auto& session, std::string_view datagram)
    {
        session->async_send(datagram);
    });
    if (!server.start("127.0.0.1", 7000))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "UDP listening on 127.0.0.1:7000\n";
    std::cin.get();
    server.stop();
}
```


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
