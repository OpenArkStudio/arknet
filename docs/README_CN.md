# arknet

arknet 是面向客户端与服务端的 C++20 header-only 网络库，提供 TCP、UDP、
WebSocket、HTTP 和 TLS。端点采用 CRTP 继承与组件组合，支持 IPv4 和 IPv6，
可选择 standalone Asio 或 Boost.Asio 后端。

## 开始使用

先[安装与配置](get-started/installation_CN.md)，再构建
[第一个客户端与服务端](get-started/first-application_CN.md)。CMake 接口目标
传递头文件路径、编译设置和依赖；arknet 没有需要构建的二进制库。

## 功能

| 能力 | 用途 | 从这里开始 |
| --- | --- | --- |
| TCP | 有序字节流与应用自定义消息边界 | [TCP](features/tcp/index_CN.md) |
| UDP | 原生数据报与来源端点 Session | [UDP](features/udp/index_CN.md) |
| WebSocket / WSS | 完整消息与 HTTP Upgrade | [WebSocket](features/websocket/index_CN.md) |
| HTTP / HTTPS | HTTP/1.1 请求、响应、路由与中间件 | [HTTP](features/http/index_CN.md) |
| TLS / mTLS | 加密传输与证书校验 | [TLS](features/tls/index_CN.md) |

每个模块包含客户端与服务端程序、配置、限制和性能入口。
[能力与支持状态](features/index_CN.md)列出依赖与计划中的功能。

## 使用 arknet

- [核心概念](concepts/index_CN.md)：端点、IO 模型、生命周期与发送所有权。
- [使用指南](guides/index_CN.md)：缓冲、外部 IO、重连、定时器、路由与 IPv6。
- [示例](examples/index_CN.md)：完整程序与构建命令。
- [参考](reference/api_CN.md)：公开类型、常用操作与配置项。

## 测量与开发

[性能](performance/index_CN.md)提供本地测量的吞吐量、RTT、CPU 占用、机器配置
和原始数据。[测试](project/testing_CN.md)记录正确性检查及覆盖范围。

[项目](project/index_CN.md)包含架构、贡献规范与路线图。HTTP/2、HTTP/3、KCP、
named pipe、Unix domain socket 和 RPC 仍为 TODO。

[源码](https://github.com/OpenArkStudio/arknet) ·
[BSL-1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE) ·
[第三方声明](project/third-party-notices_CN.md)
