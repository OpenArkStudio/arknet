# arknet

[中文](README_CN.md)

arknet is a C++20, header-only networking library maintained by OpenArkStudio.

## Features

- **Protocols:** TCP, UDP, WebSocket/WSS and HTTP/HTTPS with routing; optional TLS/mTLS.
- **Addresses:** IPv4 and IPv6 through the same client and server interfaces.
- **Runtime:** reconnect, timers, post, bounded sends, and owned or external IO contexts.
- **Extension:** CRTP inheritance, component composition and custom sessions.

## Requirements

- **C++20:** a compatible compiler and standard library.
- **CMake** 3.21+: for the provided builds, examples and tests.
- **standalone Asio** 1.38+: the default networking backend.
- **Boost** 1.90+: an alternative to standalone Asio, providing Asio and Beast.
- **OpenSSL** 1.1.1+: required only for TLS, HTTPS and WSS.

Choose either standalone Asio or Boost. [Installation and configuration](docs/guide.md)
covers dependency setup and CMake options.

## Quick Start

Build the examples and tests, then run the complete checks:

```sh
cmake -S . -B build -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
cmake --build build --target arknet_check
```

Use the `arknet::arknet` CMake interface target to configure applications.

- `ARKNET_USE_BOOST_ASIO=ON`: select Boost.Asio and Boost.Beast.
- `ARKNET_ENABLE_SSL=ON`: enable TLS, HTTPS and WSS.

## Documentation

- [Documentation and protocol guides](docs/README.md)
- [Local performance report](docs/performance/overview.md)
- [Roadmap and TODOs](docs/roadmap.md), including the planned C++ KCP implementation
- [Online documentation](https://openarkstudio.github.io/arknet/)

## License

Licensed under [BSL-1.0](LICENSE). See [third-party notices](docs/THIRD_PARTY_NOTICES.md).
