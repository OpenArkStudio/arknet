# 创建 WebSocket 客户端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动[对应服务端](server_CN.md)，再在另一个终端运行本页客户端。

## 二进制回显客户端

客户端使用 `/echo` 完成升级，发送一条二进制消息，并验证回复的内容和类型。主线程等待回显，IO 回调不阻塞等待。

```cpp
#include <arknet/websocket/ws_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

int main()
{
    std::promise<std::pair<std::string, bool>> reply;
    auto received = reply.get_future();
    bool received_once = false;

    arknet::ws_client client;
    client.set_auto_reconnect(false);
    client.bind_connect([&]
    {
        if (!arknet::get_last_error())
            client.ws_stream().binary(true);
    });
    client.bind_recv([&](std::string_view bytes)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value({std::string(bytes), client.ws_stream().got_binary()});
        }
    });
    if (!client.start("127.0.0.1", 7001, "/echo"))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("a\0b", 3)))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    bool matched = false;
    if (ready)
    {
        auto [bytes, binary] = received.get();
        matched = binary && bytes == std::string("a\0b", 3);
    }
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

发送文本时，保留默认模式，或在 IO 通道上于发送前设置 `ws_stream().text(true)`。文本负载必须是有效 UTF-8。使用 `std::string{}` 发送空消息，使用带长度的 string、view 或 buffer 发送二进制数据。接收回调按重组后的完整消息触发，不按单帧触发。

客户端 `async_start(host, port, target)` 发起连接和升级而不等待。返回值只表示发起结果；`bind_upgrade` 报告升级结果，成功的 `bind_connect` 表示端点可用。在对应回调中读取 `get_last_error()`，成功后再发送。


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
