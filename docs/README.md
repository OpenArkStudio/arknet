# arknet

arknet is a general-purpose C++20 header-only networking library for clients and
servers using TCP, UDP, RUDP, WebSocket, HTTP and TLS, with native MySQL and Redis clients. It combines
CRTP endpoints with reusable components, supports IPv4 and IPv6, and runs on
Boost.Asio by default or Standalone Asio for supported capabilities.

## Get started

<CardGrid>

<LinkCard title="Install and configure" href="get-started/installation.md" description="Dependencies, CMake and direct-header integration." />
<LinkCard title="First client and server" href="get-started/first-application.md" description="Build and verify a TCP echo." />

</CardGrid>

The CMake interface target supplies headers, compile settings and dependencies; arknet has no compiled library.

## Features

| Capability | Use it for | Start here |
| --- | --- | --- |
| TCP | Ordered streams with application-defined message boundaries | [TCP](features/tcp/index.md) |
| UDP | Native datagrams and endpoint-based sessions | [UDP](features/udp/index.md) |
| RUDP | Reliable, ordered messages over UDP with ARQ and optional FEC | [RUDP](features/rudp/index.md) |
| WebSocket / WSS | Complete messages and HTTP Upgrade | [WebSocket](features/websocket/index.md) |
| HTTP / HTTPS | HTTP/1.1 requests, responses, routing and middleware | [HTTP](features/http/index.md) |
| TLS / mTLS | Encrypted transport and certificate verification | [TLS](features/tls/index.md) |
| MySQL | Native queries, prepared parameters, transactions and exclusive leases | [MySQL](features/mysql/index.md) |
| Redis | Binary-safe RESP2/RESP3 commands, pipelines and exclusive leases | [Redis](features/redis/index.md) |

Transport guides include client and server programs; service guides cover clients and pools.
Each guide records configuration, limits and performance.
[Capabilities and support](features/index.md) records dependencies and planned features.

## Build with arknet

<CardGrid>

<LinkCard title="Core concepts" href="concepts/index.md" description="Endpoints, IO models, lifecycle and owned sends." />
<LinkCard title="Guides" href="guides/index.md" description="Socket buffers, external IO, reconnect, timers, routing and IPv6." />
<LinkCard title="Examples" href="examples/index.md" description="Complete programs and build commands." />
<LinkCard title="Reference" href="reference/api.md" description="Public types, common operations and configuration." />

</CardGrid>

## Measure and contribute

[Performance](performance/index.md) provides local results with throughput, RTT,
CPU utilization, host configuration and raw data. [Testing](project/testing.md)
records correctness checks and their coverage.

[Project](project/index.md) covers architecture, contribution guidelines and the
roadmap. HTTP/2, HTTP/3, named pipes, Unix domain sockets and RPC remain TODO.
[Release notes](project/releases.md) cover published v0.2.2 and earlier tags; current-main documentation and test scopes are identified separately.
Deployment capacity and endurance are covered by [production acceptance](project/production-readiness.md).

[Source](https://github.com/OpenArkStudio/arknet) ·
[BSL-1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE) ·
[Third-party notices](project/third-party-notices.md)
