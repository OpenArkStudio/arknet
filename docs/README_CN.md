# arknet

arknet 是 C++20 header-only 网络库，提供 TCP、UDP、WebSocket、HTTP/HTTPS 和
TLS/mTLS。客户端与 Session 采用 CRTP 继承和组件组合。可以选择 standalone
Asio 或 Boost.Asio，使用内置工作线程，也可以接入应用自己的 IO context。
IPv4 和 IPv6 共用端点 API，详见[地址与监听策略](addressing_CN.md)。

## 环境与依赖

- **C++20**：支持该标准的编译器和标准库。
- **CMake** 3.21+：用于仓库提供的构建配置、示例和测试。
- **standalone Asio** 1.38+：默认网络后端。
- **Boost** 1.90+：可替代 standalone Asio，提供 Asio 和 Beast。
- **OpenSSL** 1.1.1+：仅在启用 TLS、HTTPS 或 WSS 时需要。

standalone Asio 与 Boost 二选一。standalone 后端内置适配的 Beast 头文件，
无须另外安装 Boost。

## 从这里开始

- [安装与配置](guide_CN.md)：依赖安装、后端选择和 CMake 选项。
- [第一个客户端与服务端](quickstart_CN.md)：构建、运行基本示例。
- [公共 API 索引](api_CN.md)：公开类型和常用操作。
- [生命周期与发送](runtime_CN.md)：数据所有权、取消和错误处理。
- [本地性能报告](performance/overview_CN.md)：吞吐量、p99 延迟、IO 扩展性能、测试机器和原始结果。

## 协议指南

每种能力分别提供介绍、可运行的用法、限制和可复现的性能测量。

| 能力 | 介绍 | 使用 | 性能 |
| --- | --- | --- | --- |
| TCP 与拆包 | [TCP](tcp/overview_CN.md) | [客户端、服务端与自定义 Session](tcp/usage_CN.md) | [TCP 测量](tcp/performance_CN.md) |
| UDP 与 cast | [UDP](udp/overview_CN.md) | [数据报与端点 Session](udp/usage_CN.md) | [UDP 测量](udp/performance_CN.md) |
| WebSocket / WSS | [WebSocket](websocket/overview_CN.md) | [消息与 Upgrade](websocket/usage_CN.md) | [WS / WSS 测量](websocket/performance_CN.md) |
| HTTP / HTTPS | [HTTP](http_CN.md) | [消息](http/usage_CN.md)、[路由](http/routing_CN.md) | [HTTP / HTTPS 测量](http/performance_CN.md) |
| TLS / mTLS | [TLS](tls/overview_CN.md) | [信任、证书与身份](tls/usage_CN.md) | [安全传输](tls/performance_CN.md) |

## 理解与验证

- [IO 模型与协程](threading_CN.md)：单 context 的单线程、多线程和多 context 分片。
- [架构](design_CN.md)：CRTP、组件和扩展边界。
- [测试](testing_CN.md)与[本地性能报告](performance/overview_CN.md)：方法、图表和原始证据。
- [故障排查](troubleshooting_CN.md)与[参与开发](contributing_CN.md)。

## 计划中的能力

HTTP/2、HTTP/3、named pipe、Unix domain socket、RPC 和 C++ KCP 实现列在
[路线图](roadmap_CN.md)中。TODO 条目尚未提供可用 API。

[源码](https://github.com/OpenArkStudio/arknet) ·
[BSL-1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE) ·
[第三方声明](THIRD_PARTY_NOTICES_CN.md)
