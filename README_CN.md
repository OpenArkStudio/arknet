<p align="right">简体中文 · <a href="README.md">English</a></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/OpenArkStudio/.github/main/assets/arknet-logo-dark.svg">
    <img src="https://raw.githubusercontent.com/OpenArkStudio/.github/main/assets/arknet-logo-light.svg" alt="arknet" width="451">
  </picture>
</p>

<h3 align="center">C++20 header-only 网络库。</h3>

<p align="center">连接应用，交换消息，访问数据库。</p>

<p align="center">
  <a href="https://github.com/OpenArkStudio/arknet/actions/workflows/ci.yml?query=event%3Amerge_group"><img src="https://github.com/OpenArkStudio/arknet/actions/workflows/ci.yml/badge.svg?event=merge_group" alt="合并队列检查"></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/C%2B%2B-20-5267df" alt="C++20"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-BSL--1.0-5267df" alt="BSL-1.0"></a>
</p>

<p align="center"><a href="docs/get-started/installation_CN.md"><strong>开始使用 →</strong></a> &nbsp; · &nbsp; <a href="https://openarkstudio.github.io/arknet/zh/">在线文档</a> &nbsp; · &nbsp; <a href="examples/">示例</a> &nbsp; · &nbsp; <a href="docs/project/roadmap_CN.md">路线图</a></p>

---

## 功能

- **网络通信**：TCP、UDP、HTTP/1 路由与 WebSocket，支持 IPv4/IPv6 及可选的 TLS/mTLS。
- **可靠 UDP**：RUDP 兼容 KCP 的消息报文，支持可选的固定或自适应 Reed–Solomon FEC。
- **数据库客户端**：原生单节点 MySQL、Redis 客户端及连接池。
- **组件组合**：CRTP、可复用组件与自定义 Session，支持传输端点重连、定时器、发送队列限额和外部 IO context。

## 开始使用

需要支持 **C++20** 的工具链和 **CMake 3.21+**，默认依赖 **Boost 1.90+**（Asio 与 Beast）。MySQL 认证和安全传输需要 **OpenSSL 1.1.1+**，并设置 `ARKNET_ENABLE_SSL=ON`。

安装依赖后，在仓库根目录构建并运行测试：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARKNET_ENABLE_SSL=ON
cmake --build build --config Release --parallel 3
cmake --build build --target arknet_check --config Release
```

设置 `ARKNET_USE_BOOST_ASIO=OFF` 可选择 **Standalone Asio 1.38+**；HTTP 和 WebSocket 需要 Boost。明文 Redis 不需要 OpenSSL。

在应用中链接 header-only CMake 接口目标 `arknet::arknet` 即可集成，详见[安装与配置](docs/get-started/installation_CN.md)和[首个客户端与服务端](docs/get-started/first-application_CN.md)。

[协议指南](docs/features/index_CN.md) · [性能报告](docs/performance/index_CN.md) · [版本说明](docs/project/releases_CN.md)

---

[BSL-1.0](LICENSE) · [第三方声明](docs/project/third-party-notices_CN.md) · [OpenArkStudio](https://github.com/OpenArkStudio)
