# 创建 TCP 客户端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动[对应服务端](server_CN.md)，再在另一个终端运行本页客户端。

## 回显客户端

客户端发送一条消息，验证回显后退出。连接失败、发送未能入队，或五秒内未收到正确回显时，返回非零状态。

```cpp
#include <arknet/tcp/tcp_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

int main()
{
    std::promise<std::string> reply;
    auto received = reply.get_future();
    bool received_once = false;

    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view bytes)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(std::string(bytes));
        }
    });
    if (!client.start("127.0.0.1", 7000, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("hello TCP")))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    const bool matched = ready && received.get() == "hello TCP";
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
