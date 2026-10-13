# 缓冲区与流量控制

TCP 和 UDP 都使用有限的 socket 缓冲区。缓冲大小、突发流量和接收处理速度共同决定排队情况；本机回环也会经过这些队列。

## 三种缓冲区

| 层级 | 配置 | 容量不足时 |
| --- | --- | --- |
| arknet 应用发送队列 | `set_max_send_buffer_size(bytes)`；默认 16 MiB、最多 1024 个操作 | `async_send()` 拒绝提交，返回 `no_buffer_space` |
| 内核 socket 发送／接收缓冲 | `SO_SNDBUF`／`SO_RCVBUF` | TCP 排队并产生背压；UDP 接收队列满时丢弃数据报 |
| 应用接收与协议解析缓冲 | 客户端／服务端构造参数中的 initial／maximum | 受协议解析或单次接收容量限制；不能扩大内核接收队列 |

提高应用发送队列上限不能扩大 `SO_RCVBUF`。增加 IO 线程也不能让同一 UDP socket 的串行接收回调并行运行。

## TCP 与 UDP 的区别

| 传输方式 | 接收端处理不及时的结果 | 发送成功的含义 |
| --- | --- | --- |
| TCP，包括 TLS、HTTP 和 WebSocket 的底层连接 | 接收窗口减小或关闭，发送端减速；网络丢失通过重传恢复，应用可能观察到延迟增加或超时 | 数据交给本机传输层；不表示对端应用已处理 |
| 原生 UDP | 队列满后丢弃后续数据报，没有自动反馈、重传或顺序保证 | 本机接受了数据报；不表示对端收到 |

TCP 的 ACK 确认传输层收到数据，接收窗口负责表达剩余接收容量。它们都不确认应用业务处理完成。TCP 连接仍可能因超时、断连或资源不足而失败。

<Aside type="caution" title="注意：本机回环也可能丢失 UDP 数据报">

UDP 接收队列满后会丢弃数据报，本机回环也不例外。发送成功不确认对端收到。缓冲容量需要结合突发流量和接收处理耗时设置。

</Aside>

arknet 不对原生 UDP 自动切包、重传或限速。需要在 UDP 上可靠、有序地传递消息时，使用 [RUDP 模块](../features/rudp/index_CN.md)。

## 设置并读取实际大小

客户端在 `bind_init` 中设置，此时 socket 已打开、尚未连接；重连时会重新执行。TCP 服务端对每个已接受的连接设置。UDP 服务端的所有 Session 共用一个 socket，应在服务端 `bind_init` 中设置一次。

| 对象 | 设置 API | 查询 API |
| --- | --- | --- |
| TCP／UDP 客户端、TCP Session 及其 TLS／HTTP／WebSocket 派生对象 | `set_sndbuf_size(bytes)`、`set_rcvbuf_size(bytes)` | `get_sndbuf_size()`、`get_rcvbuf_size()` |
| UDP 服务端 | `acceptor().set_option(asio::socket_base::send_buffer_size(bytes))` 或 `receive_buffer_size(bytes)` | `acceptor().get_option(option)` |

<Aside type="tip" title="建议：读取实际缓冲大小">

以下 **4 MiB 只是配置示例，不是推荐给所有业务的固定值**。每次调用后立即检查错误，并记录实际大小：操作系统可能限制或拒绝请求值。查询会更新线程局部的 `get_last_error()`。

</Aside>

### TCP 或 UDP 客户端

```cpp
#include <iostream>

#include <arknet/arknet.hpp>

arknet::udp_client client; // TCP 可改为 arknet::tcp_client。
client.bind_init([&] {
    client.set_rcvbuf_size(4 * 1024 * 1024);
    if (const auto error = arknet::get_last_error()) {
        std::cerr << "SO_RCVBUF: " << error.message() << '\n';
        client.request_stop();
        return;
    }
    const int actual = client.get_rcvbuf_size();
    if (const auto error = arknet::get_last_error()) {
        std::cerr << "getsockopt: " << error.message() << '\n';
        client.request_stop();
        return;
    }
    std::cout << "SO_RCVBUF=" << actual << " bytes\n";
});
```

### TCP 服务端

```cpp
arknet::tcp_server server;
server.bind_accept([](auto& session) {
    if (arknet::get_last_error()) return;
    session->set_rcvbuf_size(4 * 1024 * 1024);
    if (const auto error = arknet::get_last_error()) {
        std::cerr << "SO_RCVBUF: " << error.message() << '\n';
        session->stop();
        return;
    }
    const int actual = session->get_rcvbuf_size();
    if (const auto error = arknet::get_last_error()) {
        std::cerr << "getsockopt: " << error.message() << '\n';
        session->stop();
        return;
    }
    std::cout << "SO_RCVBUF=" << actual << " bytes\n";
});
```

这个回调在 TCP 接受连接后运行。它不改变已经完成的握手所协商的 TCP window scaling；需要控制握手前的参数时，应验证目标系统的监听 socket 设置和继承行为。

### UDP 服务端

```cpp
arknet::udp_server server;
server.bind_init([&] {
    arknet::error_code error;
    server.acceptor().set_option(
        asio::socket_base::receive_buffer_size(4 * 1024 * 1024), error);
    if (error) {
        std::cerr << "SO_RCVBUF: " << error.message() << '\n';
        server.request_stop();
        return;
    }
    asio::socket_base::receive_buffer_size actual;
    server.acceptor().get_option(actual, error);
    if (error) {
        std::cerr << "getsockopt: " << error.message() << '\n';
        server.request_stop();
        return;
    }
    std::cout << "SO_RCVBUF=" << actual.value() << " bytes\n";
});
```

这些片段配置 socket；完整的启动、收发和关闭程序见 [TCP 使用](../features/tcp/configuration_CN.md)和 [UDP 使用](../features/udp/configuration_CN.md)。发送缓冲使用同样的方法，改为 `set_sndbuf_size` 或 `send_buffer_size`。

## 按负载选择容量

- UDP 多客户端向同一个服务端突发发送时，估算总突发量：客户端数 × 每客户端在途数量 × 数据报字节数。接收队列还要容纳内核管理开销，不能把 `SO_RCVBUF` 简单换算成精确包数。
- 允许的接收停顿时间为 T、输入速率为 R 时，至少评估 R × T 字节的积压，并用真实突发和回调耗时测量余量。短突发结束后必须能及时清空队列。
- TCP 长距离高速连接可参考带宽 × RTT 的在途量。先测量操作系统的自动调优，再决定是否手动设置；部分系统手动设置后会限制自动调优。
- 多个 TCP 连接各有自己的 socket 缓冲预算；UDP 服务端 Session 共用接收队列。容量增大会提高内存预算和潜在排队延迟，不会提高业务处理速度。

持续输入速度超过处理速度时，任何有限缓冲最终都会耗尽。此时需缩短接收回调、调整业务并发或采用适合业务的流量控制；要求可靠交付时使用 TCP 或可靠 UDP 协议。

## 操作系统限制与诊断

| 平台 | 检查内容 |
| --- | --- |
| Linux | `sysctl net.core.rmem_max net.core.wmem_max`；TCP 自动调优还涉及 `net.ipv4.tcp_rmem`／`tcp_wmem`；用 `nstat` 或 `netstat -su` 查看 UDP 接收错误与缓冲溢出 |
| macOS | `sysctl kern.ipc.maxsockbuf net.inet.udp.recvspace net.inet.udp.sendspace`；`netstat -s -p udp` 中查看 `dropped due to full socket buffers` |
| Windows | 使用 socket 选项查询实际大小，并结合进程、网络性能计数器和抓包定位丢失；不要照搬 POSIX sysctl 数值 |

请求值可能被限制或拒绝，始终记录实际值。Linux 对 `SO_RCVBUF`／`SO_SNDBUF` 的查询值通常包含内核按请求值加倍的管理预算，不能直接与 macOS 的数值比较有效包体容量。内核统计可能是系统全局计数，应在隔离负载前后取差值，并结合应用的发送／接收计数定位。

[2026-10-12 UDP 诊断](../performance/udp-update_CN.md#%E6%8E%A5%E6%94%B6%E7%BC%93%E5%86%B2%E9%9A%94%E7%A6%BB%E5%AE%9E%E9%AA%8C)中，16 MiB 突发超过服务端 4 MiB 接收预算，丢失 767 条；内核缓冲溢出计数也增加 767。减小突发或改变缓冲容量能改变丢失数量，1 KiB 小包的大突发也能复现。MTU 和数据报最大长度是另一项限制，见 [UDP 边界](../performance/udp-limits_CN.md)。

[生命周期与应用背压](../concepts/sending_CN.md) · [测试与指标](../performance/methodology_CN.md)
