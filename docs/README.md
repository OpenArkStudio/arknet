# arknet

arknet is a C++20 header-only networking library for applications that need
TCP, UDP, WebSocket, HTTP/HTTPS and TLS/mTLS. Clients and sessions use CRTP
inheritance and component composition. Choose standalone Asio or Boost.Asio;
use owned workers or integrate with your application's IO contexts.
IPv4 and IPv6 share the same endpoint APIs; see [addresses and listening policy](addressing.md).

## Requirements

- **C++20:** a compatible compiler and standard library.
- **CMake** 3.21+: for the provided builds, examples and tests.
- **standalone Asio** 1.38+: the default networking backend.
- **Boost** 1.90+: an alternative to standalone Asio, providing Asio and Beast.
- **OpenSSL** 1.1.1+: required only for TLS, HTTPS and WSS.

Choose either standalone Asio or Boost. The standalone backend includes adapted
Beast headers and does not require a separate Boost installation.

## Start Here

- [Installation and configuration](guide.md): dependency setup, backend selection and CMake options.
- [First client and server](quickstart.md): build and run the basic examples.
- [Public API index](api.md): public types and common operations.
- [Lifecycle and sends](runtime.md): ownership, cancellation and error handling.
- [Local performance report](performance/overview.md): throughput, p99 latency, IO scaling, hardware and raw results.

## Protocol Guides

Each guide includes its scope, working examples, limits and reproducible measurements.

| Capability | Introduction | Usage | Performance |
| --- | --- | --- | --- |
| TCP and framing | [TCP](tcp/overview.md) | [Client, server and custom sessions](tcp/usage.md) | [TCP measurements](tcp/performance.md) |
| UDP and cast | [UDP](udp/overview.md) | [Datagrams and endpoint sessions](udp/usage.md) | [UDP measurements](udp/performance.md) |
| WebSocket / WSS | [WebSocket](websocket/overview.md) | [Messages and upgrade](websocket/usage.md) | [WS / WSS measurements](websocket/performance.md) |
| HTTP / HTTPS | [HTTP](http.md) | [Messages](http/usage.md), [routing](http/routing.md) | [HTTP / HTTPS measurements](http/performance.md) |
| TLS / mTLS | [TLS](tls/overview.md) | [Trust, certificates and identity](tls/usage.md) | [Secure transports](tls/performance.md) |

## Understand and Validate

- [IO models and coroutines](threading.md): one context with one or multiple runners, or sharded contexts.
- [Architecture](design.md): CRTP, components and extension boundaries.
- [Testing](testing.md) and [local performance report](performance/overview.md): methodology, charts and raw evidence.
- [Troubleshooting](troubleshooting.md) and [contributing](contributing.md).

## Planned Capabilities

HTTP/2, HTTP/3, named pipes, Unix domain sockets, RPC and a C++ KCP implementation
are tracked in the [roadmap](roadmap.md). TODO entries are not available APIs.

[Source](https://github.com/OpenArkStudio/arknet) ·
[BSL-1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE) ·
[Third-party notices](THIRD_PARTY_NOTICES.md)
