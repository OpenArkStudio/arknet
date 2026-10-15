# arknet

[中文](README_CN.md)

arknet is a general-purpose C++20, header-only networking library maintained by OpenArkStudio.

## Features

- **Protocols:** TCP, UDP, WebSocket/WSS and HTTP/1 (HTTP/HTTPS) with routing; optional TLS/mTLS.
- **Reliable UDP:** RUDP with a C++20 ARQ core, KCP-compatible messages and optional fixed/adaptive Reed–Solomon FEC.
- **Addresses:** IPv4 and IPv6 through the same client and server interfaces.
- **Runtime:** reconnect, timers, post, bounded sends, and owned or external IO contexts.
- **Extension:** CRTP inheritance, component composition and custom sessions.

Native single-node MySQL and Redis clients are planned. Their agreed scope and
release criteria are in the [database client guide](docs/features/database-clients.md).

## Requirements

- **C++20:** a compatible compiler and standard library.
- **CMake** 3.21+: for the provided builds, examples and tests.
- **Standalone Asio** 1.38+: optional, for TCP, UDP, RUDP and TLS.
- **Boost** 1.90+: the default dependency, providing Asio and Beast for all protocols.
- **OpenSSL** 1.1.1+: required only for TLS, HTTPS and WSS.

Use Boost by default, or choose Standalone Asio for the base protocols. [Installation and configuration](docs/get-started/installation.md)
covers dependency setup and CMake options.

## Quick Start

Build the examples and tests, then run the complete checks:

```sh
cmake -S . -B build
cmake --build build --parallel 3
cmake --build build --target arknet_check
```

Use the `arknet::arknet` CMake interface target to configure applications.

- `ARKNET_USE_BOOST_ASIO=OFF`: use Standalone Asio for TCP, UDP, RUDP and TLS.
- `ARKNET_ENABLE_SSL=ON`: enable TLS, HTTPS and WSS.

## Documentation

- [Documentation and protocol guides](docs/README.md)
- [Local performance reports](docs/performance/index.md)
- [RUDP guide](docs/features/rudp/index.md) and [single HTTP requests](docs/features/http/requests.md)
- [Roadmap and TODOs](docs/project/roadmap.md)
- [Online documentation](https://openarkstudio.github.io/arknet/)

## License

Licensed under [BSL-1.0](LICENSE). See [third-party notices](docs/project/third-party-notices.md).
