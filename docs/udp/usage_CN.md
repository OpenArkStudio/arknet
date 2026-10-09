# UDP 用法

按[构建指南](../guide_CN.md)在 C++20 应用中接入 `arknet::arknet` 接口目标。UDP 不依赖 OpenSSL；当前不提供 DTLS。

## 回显服务端

服务端通过来源端点对应的 session 回复。通过 `arknet::arknet` 接口目标，将两段代码分别构建为可执行程序。先启动服务端，再在另一个终端运行客户端。在服务端终端按 Enter 停止服务端。

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

## 向多个目标发送

以下片段绑定 cast socket，并将收到的数据报回复给发送方：

```cpp
#include <arknet/udp/udp_cast.hpp>

arknet::udp_cast socket;
socket.bind_recv([&](asio::ip::udp::endpoint& sender, std::string_view datagram)
{
    socket.async_send(sender, datagram);
});
socket.start("127.0.0.1", 0);
```

启动后，使用数字端点发送，或在发送过程中异步解析 host/service：

```cpp
auto target = asio::ip::udp::endpoint(asio::ip::make_address("127.0.0.1"), 7000);
socket.async_send(target, std::string("direct"),
    [](const arknet::error_code& error, std::size_t bytes)
    {
        if (error)
            std::cerr << error.message() << '\n';
    });
auto completion = socket.async_send("localhost", 7000, std::string("resolved"),
    asio::use_future);
```

Future 返回 `(error_code, bytes)`，不能在 IO 回调内等待。完成回调也支持 `(bytes)` 或 `()` 签名，以及泛型和 move-only 回调。地址解析失败时，通过同一个请求的完成回调报告错误。需要在接收回调之后保存发送方时，应复制 endpoint。

## Socket 选项与 session 状态

在 `bind_init` 中配置原生 socket 选项，此时 socket 已打开。需要发送广播时，在启动前配置：

```cpp
socket.bind_init([&]
{
    arknet::error_code error;
    socket.socket().set_option(asio::socket_base::broadcast(true), error);
    if (error)
        std::cerr << error.message() << '\n';
});
```

绑定适合所选接口的地址，再向该网络的广播端点发送。组播需要使用 `asio::ip::multicast` 选项，包括接收端加入组，以及按需选择接口。数据包是否被路由或接受，由网络和操作系统决定。Socket 缓冲区选项不会改变 UDP 可靠性或应用发送队列；测量其影响时，应查询实际缓冲区大小。

通过 `udp_session_t<Derived>` 定义每个对端的状态，再使用 `udp_server_t<Session>` 创建服务端。启动前配置空闲过期规则：

```cpp
server.bind_connect([](auto& session)
{
    session->set_silence_timeout(std::chrono::seconds(30));
});
```

首个数据报在 connect 回调之后交付。Session 的键是来源 IP 和端口，不是经过认证的身份。应用需要定义重复、乱序、丢失以及来源端点变化的处理规则。

## 所有权、容量与关闭

服务端、客户端和 cast 回调中的接收 view 都是借用数据。被接受的发送复制或持有输入字节，并保留目标直到完成。`async_send` 返回入队是否成功，不表示对端收到。立即拒绝可能在提交线程调用完成回调；被接受的发送在 IO 通道完成。通过完成参数或对应生命周期回调读取错误，因为 `get_last_error()` 是线程局部状态。

发送队列默认限制每个对象 16 MiB 和 1024 个操作，使用 `set_max_send_buffer_size` 修改字节上限。容量不足时报告 `no_buffer_space`。接收上限必须至少为 65536 字节。

过大的发送可能由操作系统报告 `message_size` 或其他原生 socket 错误；Windows 可能返回系统错误 1784。完成回调保留该错误码，并报告 0 字节。不要无限重试同样大小的数据报。

自行运行调度线程时，将外部 `asio::io_context&` 传给端点构造函数。一个 UDP 服务端的所有 session 共享 socket 的串行通道。保持 context 运行直到端点停止。回调内调用 `request_stop()`，所有者线程调用 `stop()` 或 `wait_stopped()`，随后先销毁端点，再停止外部 context。另见 [IO 模型](../threading_CN.md)、[UDP 回归测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/udp.cpp)及[性能](performance_CN.md)。
