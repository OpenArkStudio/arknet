# arknet

[English](README.md)

arknet 是 OpenArkStudio 维护的通用 C++20 header-only 网络库。

## 功能

- **传输协议**：TCP、UDP、WebSocket/WSS、HTTP/1 与路由，以及可选的 TLS/mTLS；支持 IPv4 和 IPv6。
- **可靠 UDP**：RUDP，提供 C++20 ARQ 核心、兼容 KCP 的消息报文，以及可选的固定或自适应 Reed–Solomon FEC。
- **MySQL**：原生单节点查询、预处理参数、事务与连接池独占租约。
- **Redis**：原生单节点 RESP2/RESP3 命令、流水线、认证与连接池独占租约。
- **运行机制**：传输端点自动重连、定时器、post、发送队列限额，以及内置或外部 IO context。
- **扩展方式**：CRTP 继承、组件组合与自定义 Session。

## 环境与依赖

- 支持 **C++20** 的编译器和标准库。
- **CMake 3.21+**：用于仓库提供的构建配置、示例和测试。
- **Boost 1.90+**：默认依赖，提供 Boost.Asio 和 Boost.Beast。
- **Standalone Asio 1.38+**：可用于 TCP、UDP、RUDP、TLS 和数据库客户端。HTTP 与 WebSocket 需要 Boost。
- **OpenSSL 1.1.1+**：用于 MySQL 认证，以及 TLS、HTTPS 和 WSS。

arknet 没有需要编译的二进制库。`arknet::arknet` CMake 接口目标提供头文件、编译设置和依赖要求，详见[安装与配置](docs/get-started/installation_CN.md)。

## 构建与测试

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARKNET_ENABLE_SSL=ON
cmake --build build --parallel 3
cmake --build build --target arknet_check --config Release
```

默认使用 Boost。设置 `ARKNET_USE_BOOST_ASIO=OFF` 可选择 Standalone Asio。MySQL 和安全传输需要启用 `ARKNET_ENABLE_SSL`；明文 Redis 不需要此选项。

## 文档

- [入门与协议指南](docs/README_CN.md)
- [MySQL 与 Redis 客户端](docs/features/database-clients_CN.md)
- [本地性能报告](docs/performance/index_CN.md)
- [版本说明](docs/project/releases_CN.md)与[路线图](docs/project/roadmap_CN.md)
- [在线文档](https://openarkstudio.github.io/arknet/zh/)

## 许可证

采用 [BSL-1.0](LICENSE)，依赖声明见[第三方声明](docs/project/third-party-notices_CN.md)。
