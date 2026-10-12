# 创建 UDP 客户端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动[对应服务端](server_CN.md)，再在另一个终端运行本页客户端。

## 回显客户端

客户端将本地 UDP socket 关联到服务端，并发送一个二进制数据报。主线程最多等待五秒；超时不代表 UDP 交付保证。收到额外数据报时，不会重复设置同一个 promise。

```cpp
#include <arknet/udp/udp_client.hpp>
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

    arknet::udp_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](std::string_view datagram)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(std::string(datagram));
        }
    });
    if (!client.start("127.0.0.1", 7000))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("a\0b", 3)))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    const bool matched = ready && received.get() == std::string("a\0b", 3);
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

使用 `std::string{}` 发送空数据报。二进制数据应使用带长度的 string、view、span 或 buffer；裸字符指针按空字符结尾计算长度。指针加长度的请求即使长度为零，也不能传入空指针。

客户端 `async_start(host, port)` 发起解析和本地 socket 关联而不等待，在 `bind_connect` 中通过 `get_last_error()` 读取完成结果。Cast 的 `async_start(local_host, local_port)` 通过 `bind_start` 报告完成。两者都不会执行远端 UDP 握手。


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
