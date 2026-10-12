# arknet

arknet is a C++20 header-only networking library for clients and servers using TCP,
UDP, WebSocket, HTTP and TLS. It combines CRTP endpoints with reusable components,
supports IPv4 and IPv6, and runs on standalone Asio or Boost.Asio.

## Get started

[Install and configure](get-started/installation.md) the library, then build your
[first client and server](get-started/first-application.md). The CMake interface
target supplies headers, compile settings and dependencies; arknet has no compiled library.

## Features

| Capability | Use it for | Start here |
| --- | --- | --- |
| TCP | Ordered streams with application-defined message boundaries | [TCP](features/tcp/index.md) |
| UDP | Native datagrams and endpoint-based sessions | [UDP](features/udp/index.md) |
| WebSocket / WSS | Complete messages and HTTP Upgrade | [WebSocket](features/websocket/index.md) |
| HTTP / HTTPS | HTTP/1.1 requests, responses, routing and middleware | [HTTP](features/http/index.md) |
| TLS / mTLS | Encrypted transport and certificate verification | [TLS](features/tls/index.md) |

Each module includes client and server programs, configuration, limits and a performance entry.
[Capabilities and support](features/index.md) records dependencies and planned features.

## Build with arknet

- [Core concepts](concepts/index.md): endpoints, IO models, lifecycle and owned sends.
- [Guides](guides/index.md): socket buffers, external IO, reconnect, timers, routing and IPv6.
- [Examples](examples/index.md): complete programs and build commands.
- [Reference](reference/api.md): public types, common operations and configuration.

## Measure and contribute

[Performance](performance/index.md) provides local results with throughput, RTT,
CPU utilization, host configuration and raw data. [Testing](project/testing.md)
records correctness checks and their coverage.

[Project](project/index.md) covers architecture, contribution guidelines and the
roadmap. HTTP/2, HTTP/3, KCP, named pipes, Unix domain sockets and RPC remain TODO.

[Source](https://github.com/OpenArkStudio/arknet) ·
[BSL-1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE) ·
[Third-party notices](project/third-party-notices.md)
