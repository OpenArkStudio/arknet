# UDP 配置与进阶使用

[客户端](client_CN.md)与[服务端](server_CN.md)示例提供完整的启动、收发与关闭程序。以下配置在相应对象启动前应用，除非另有说明。

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

UDP 客户端默认不开启 `SO_REUSEADDR`，确保每个客户端独占本地端点。Linux 下开启该选项可能让已连接的 UDP socket 共用来源端口，导致回包被投递给其他客户端。需要复用时，可在 `bind_init` 中显式调用 `client.set_reuse_address(true)`。

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

自行运行调度线程时，将外部 `asio::io_context&` 传给端点构造函数。一个 UDP 服务端的所有 session 共享 socket 的串行通道。保持 context 运行直到端点停止。回调内调用 `request_stop()`，所有者线程调用 `stop()` 或 `wait_stopped()`，随后先销毁端点，再停止外部 context。另见 [IO 模型](../../concepts/io-models_CN.md)、[UDP 回归测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/udp.cpp)及[性能](performance_CN.md)。
