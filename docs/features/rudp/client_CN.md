# 创建 RUDP 客户端

先启动[回显服务端](server_CN.md)，再运行以下完整客户端程序。

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

#include <arknet/rudp/rudp_client.hpp>

int main()
{
    std::promise<std::string> reply;
    auto received = reply.get_future();
    bool received_once = false;
    arknet::rudp_client client;
    client.set_auto_reconnect(false);
    client.set_connect_timeout(std::chrono::seconds(2));
    client.bind_recv([&](std::string_view message)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(std::string(message));
        }
    });
    if (!client.start("127.0.0.1", 7000))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    const std::string payload(4119, 'r');
    auto accepted = client.async_send(payload, asio::use_future);
    auto [error, bytes] = accepted.get();
    const bool ready = !error && bytes == payload.size() &&
        received.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    const bool matched = ready && received.get() == payload;
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

与原生 UDP 不同，`start()` 等待远端 RUDP 握手完成。`async_start()` 通过
`bind_connect` 报告完成结果，在该回调中读取 `get_last_error()`。
`bind_handshake` 直接接收 `error_code`。

示例中的 future 确认队列接纳，收到的回显确认本次交付。重传次数有限，持续断网时
不能保证交付。根据业务配置[自动重连](../../guides/reconnect_CN.md)，新连接使用
新的 conversation，队列重新开始，不重放旧消息。
