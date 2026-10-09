# TCP 用法

按[构建指南](../guide_CN.md)在 C++20 应用中接入 `arknet::arknet` 接口目标。TCP 不依赖 OpenSSL。

## 回显服务端

通过 `arknet::arknet` 接口目标，将服务端和客户端代码块分别构建为 C++20 可执行程序。先启动服务端，再在另一个终端运行客户端。在服务端终端按 Enter 停止服务端。两端均使用 `use_dgram` 长度拆包；客户端在主线程等待回显，不在 IO 回调中等待。

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

## 选择接收策略

行协议可在两端接收器启动时传入 `'\n'`，发送时自行携带终止符：

```cpp
server.start("127.0.0.1", 7000, '\n');
client.start("127.0.0.1", 7000, '\n');
client.async_send(std::string("command\n"));
```

回调数据包含 `\n`。还可以传入多字节分隔符或自定义 Asio 匹配条件。分隔符只控制读取，`async_send` 不会自动补上分隔符。不传接收策略时，应在应用缓冲区中累积字节，解析所有完整消息，并保留未完成的尾部。使用 `use_dgram` 时，无需自行添加长度前缀。

通过 `tcp_client(initial_bytes, maximum_bytes, workers)` 或对应的服务端构造函数设置接收缓冲区上限。应限制始终不完成一条消息的对端所能占用的缓冲区，并通过 `set_silence_timeout` 设置 session 空闲超时。

## 发送、重连与错误

在 `start` 前配置监听回调和容量限制。

使用客户端 `async_start(host, port, options...)` 可发起连接而不等待。返回值只表示发起结果；在 `bind_connect` 中通过 `get_last_error()` 读取最终结果，成功后再发送。

以下代码放在客户端启动之前：

```cpp
client.set_connect_timeout(std::chrono::seconds(10));
client.set_auto_reconnect(true, std::chrono::seconds(1));
client.set_max_send_buffer_size(1024 * 1024);
client.bind_disconnect([]
{
    std::cerr << arknet::get_last_error().message() << '\n';
});
```

```cpp
client.async_send(std::string("request"),
    [](const arknet::error_code& error, std::size_t bytes)
    {
        if (error)
            std::cerr << error.message() << '\n';
        else
            std::cout << "written: " << bytes << '\n';
    });
```

完成回调也可使用 `(bytes)` 或 `()` 签名，但这两种签名不提供错误参数。传入 `asio::use_future` 会返回 `std::future<std::pair<arknet::error_code, std::size_t>>`；只能在 IO 回调之外等待。立即拒绝时，完成回调可能在 `async_send` 返回前就在提交线程执行。被接受的操作在对象的 IO 通道完成。优先读取完成回调的错误参数；`get_last_error()` 是线程局部状态。

每个对象默认最多排队 16 MiB 负载和 1024 个操作。超出容量时返回 `no_buffer_space`。通过 `get_queued_send_buffer_size()` 观察排队字节数，在队列满时暂停生产者，避免在 IO 回调中循环重试。重连不会重放旧发送；需要重试业务请求时，应设计请求 ID 和应答确认机制。

## 自定义 session 与外部 IO

```cpp
class peer_session : public arknet::tcp_session_t<peer_session>
{
public:
    using arknet::tcp_session_t<peer_session>::tcp_session_t;
    std::string user_id;
};

arknet::tcp_server_t<peer_session> server;
```

在 session 的 IO 通道访问业务状态，跨通道访问时进行同步。需要保存接收数据或提交到 CPU executor 时，应先复制 view。在接收回调内调用 `async_send(bytes)` 时，被接受的发送操作已经持有负载。

使用外部调度器时，将 `asio::io_context&` 传给客户端或服务端构造函数。阻塞式 `start` 前，应启动 context 的运行线程并保留 work guard。只传入一个 context 的服务端共享一个执行通道。`std::vector<asio::io_context*>` 中的多个条目提供独立串行通道，即使条目指向同一个 context。完整规则见 [IO 模型](../threading_CN.md)。

在外部 context 仍运行时，由所有者线程调用 `stop()` 或 `wait_stopped()`。IO 回调内使用 `request_stop()`，随后由所有者线程等待完成。`wait_stopped()` 会拒绝 IO 工作线程内的调用。先停止并销毁网络对象，再释放 work guard、等待运行线程退出。

另见[生命周期测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/lifecycle.cpp)、[TCP 测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/tcp.cpp)及[性能](performance_CN.md)。
