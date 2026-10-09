# 第一个客户端与服务端

[安装](guide_CN.md)

下面的完整程序启动本地 TCP 回显服务端与客户端，验证回复后停止两者。
先按[安装与配置](guide_CN.md)安装依赖，并接入 `arknet::arknet` CMake 接口目标。
arknet 是 header-only 库；接口目标只传递构建配置和依赖，不生成 arknet 二进制库。

```cpp
#include <arknet/arknet.hpp>
#include <chrono>
#include <future>
#include <string>
using namespace std::chrono_literals;

int main()
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data)
    {
        session->async_send(data);
    });
    if (!server.start("127.0.0.1", 0, arknet::use_dgram))
        return 1;

    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    std::promise<std::string> reply;
    client.bind_recv([&](std::string_view data)
    {
        reply.set_value(std::string(data));
    });
    if (!client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram))
        return 2;
    auto result = reply.get_future();
    if (!client.async_send("hello"))
        return 3;
    const bool ready = result.wait_for(5s) == std::future_status::ready;
    const bool correct = ready && result.get() == "hello";
    client.stop();
    server.stop();
    return correct ? 0 : 4;
}
```

两端都使用 arknet 的 `use_dgram` 消息拆包。TCP 本身是字节流，应按对端
线路协议选择[拆包方式](tcp/usage_CN.md)。等待发生在所有者线程，不在 IO 回调中。

## 下一步

按业务选择 [TCP](tcp/usage_CN.md)、[UDP](udp/usage_CN.md)、
[WebSocket](websocket/usage_CN.md)、[HTTP 与路由](http/usage_CN.md)或
[TLS](tls/usage_CN.md)。将对象用于服务前，阅读
[生命周期与发送](runtime_CN.md)；共享 context 前，阅读
[IO 模型](threading_CN.md)。
