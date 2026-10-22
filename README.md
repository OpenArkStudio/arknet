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

arknet is a general-purpose C++20, header-only networking library maintained by OpenArkStudio.

## Features

- **Transports:** TCP, UDP, WebSocket/WSS, HTTP/1 with routing, and optional TLS/mTLS; IPv4 and IPv6.
- **Reliable UDP:** RUDP with a C++20 ARQ core, KCP message compatibility, and optional fixed or adaptive Reed–Solomon FEC.
- **MySQL:** native single-node queries, prepared parameters, transactions and exclusive connection-pool leases.
- **Redis:** native single-node RESP2/RESP3 commands, pipelines, authentication and exclusive connection-pool leases.
- **Runtime:** reconnect for transport endpoints, timers, post, bounded sends, and owned or external IO contexts.
- **Extension:** CRTP inheritance, component composition and custom sessions.

## Requirements

- **C++20** compiler and standard library.
- **CMake 3.21+** for the provided builds, examples and tests.
- **Boost 1.90+** by default, providing Boost.Asio and Boost.Beast.
- **Standalone Asio 1.38+** as an alternative for TCP, UDP, RUDP, TLS and database clients. HTTP and WebSocket require Boost.
- **OpenSSL 1.1.1+** for MySQL authentication and for TLS, HTTPS and WSS.

There is no arknet binary library. The `arknet::arknet` CMake interface target supplies headers, compile settings and dependency requirements. See [installation](docs/get-started/installation.md).

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARKNET_ENABLE_SSL=ON
cmake --build build --parallel 3
cmake --build build --target arknet_check --config Release
```

Boost is the default. Set `ARKNET_USE_BOOST_ASIO=OFF` to select Standalone Asio. Enable `ARKNET_ENABLE_SSL` for MySQL and secure transports; plain Redis does not require it.

## Documentation

- [Get started and protocol guides](docs/README.md)
- [MySQL and Redis clients](docs/features/database-clients.md)
- [Local performance reports](docs/performance/index.md)
- [Release notes](docs/project/releases.md) and [roadmap](docs/project/roadmap.md)
- [Online documentation](https://openarkstudio.github.io/arknet/)

## License

[BSL-1.0](LICENSE). See [third-party notices](docs/project/third-party-notices.md).
