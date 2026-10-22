<p align="right">English · <a href="README_CN.md">简体中文</a></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="https://raw.githubusercontent.com/OpenArkStudio/.github/main/assets/arknet-logo-dark.svg">
    <img src="https://raw.githubusercontent.com/OpenArkStudio/.github/main/assets/arknet-logo-light.svg" alt="arknet" width="451">
  </picture>
</p>

<h3 align="center">A C++20 header-only networking library.</h3>

<p align="center">Connect applications. Exchange messages. Access databases.</p>

<p align="center">
  <a href="https://github.com/OpenArkStudio/arknet/actions/workflows/ci.yml?query=event%3Amerge_group"><img src="https://github.com/OpenArkStudio/arknet/actions/workflows/ci.yml/badge.svg?event=merge_group" alt="Merge queue checks"></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/C%2B%2B-20-5267df" alt="C++20"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-BSL--1.0-5267df" alt="BSL-1.0"></a>
</p>

<p align="center"><a href="docs/get-started/installation.md"><strong>Get started →</strong></a> &nbsp; · &nbsp; <a href="https://openarkstudio.github.io/arknet/">Documentation</a> &nbsp; · &nbsp; <a href="examples/">Examples</a> &nbsp; · &nbsp; <a href="docs/project/roadmap.md">Roadmap</a></p>

---

## Features

- **Networking:** TCP, UDP, HTTP/1 routing and WebSocket, with IPv4/IPv6 and optional TLS/mTLS.
- **Reliable UDP:** RUDP with KCP-compatible messages and optional fixed or adaptive Reed–Solomon FEC.
- **Database clients:** native single-node MySQL and Redis with connection pools.
- **Composition:** CRTP, reusable components and custom sessions, with transport reconnect, timers, bounded sends and external IO contexts.

## Get started

Requires a **C++20** toolchain and **CMake 3.21+**, with **Boost 1.90+** (Asio and Beast) by default. MySQL authentication and secure transports require **OpenSSL 1.1.1+** and `ARKNET_ENABLE_SSL=ON`.

With dependencies installed, build and test from the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARKNET_ENABLE_SSL=ON
cmake --build build --config Release --parallel 3
cmake --build build --target arknet_check --config Release
```

Standalone Asio **1.38+** is available with `ARKNET_USE_BOOST_ASIO=OFF`; HTTP and WebSocket require Boost. Plain Redis does not require OpenSSL.

To use arknet in your application, link to the header-only CMake interface target `arknet::arknet`. See [installation](docs/get-started/installation.md) and the [first client and server](docs/get-started/first-application.md).

[Protocol guides](docs/features/index.md) · [Performance reports](docs/performance/index.md) · [Release notes](docs/project/releases.md)

---

[BSL-1.0](LICENSE) · [Third-party notices](docs/project/third-party-notices.md) · [OpenArkStudio](https://github.com/OpenArkStudio)
