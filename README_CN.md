# arknet

[English](README.md)

arknet 是 OpenArkStudio 维护的通用 C++20 header-only 网络库。

## 功能

- **协议**：TCP、UDP、WebSocket/WSS、HTTP/HTTPS 与路由，以及可选的 TLS/mTLS。
- **可靠 UDP**：RUDP，提供 C++20 ARQ 核心、兼容 KCP 的消息报文，以及可选的固定／自适应 Reed–Solomon FEC。
- **MySQL**：原生单节点查询、预处理参数、事务与独占连接租约。
- **地址族**：IPv4 和 IPv6 使用同一套客户端、服务端接口。
- **运行机制**：自动重连、定时器、post、发送队列限额，以及内置或外部 IO context。
- **扩展方式**：CRTP 继承、组件组合和自定义 Session。

## 环境与依赖

- **C++20**：支持该标准的编译器和标准库。
- **CMake** 3.21+：用于仓库提供的构建配置、示例和测试。
- **Standalone Asio** 1.38+：默认网络依赖库。
- **Boost** 1.90+：可替代 Standalone Asio，提供 Asio 和 Beast。
- **OpenSSL** 1.1.1+：用于 MySQL 认证，以及 TLS、HTTPS 和 WSS。

Standalone Asio 与 Boost 二选一。依赖安装和 CMake 选项见[安装与配置](docs/get-started/installation_CN.md)。

## 快速开始

构建示例和测试，然后执行完整检查：

```sh
cmake -S . -B build -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
cmake --build build --target arknet_check
```

通过 `arknet::arknet` CMake 接口目标配置应用。

- `ARKNET_USE_BOOST_ASIO=ON`：选择 Boost.Asio 和 Boost.Beast。
- `ARKNET_ENABLE_SSL=ON`：启用 TLS、HTTPS 和 WSS。

## 文档

- [文档与协议指南](docs/README_CN.md)
- [本地性能报告](docs/performance/index_CN.md)
- [RUDP 指南](docs/features/rudp/index_CN.md)与[单次 HTTP 请求](docs/features/http/requests_CN.md)
- [MySQL 客户端与连接池](docs/features/mysql/index_CN.md)
- [路线图与 TODO](docs/project/roadmap_CN.md)
- [在线文档](https://openarkstudio.github.io/arknet/zh/)

## 许可证

采用 [BSL-1.0](LICENSE)，第三方依赖见[第三方声明](docs/project/third-party-notices_CN.md)。
