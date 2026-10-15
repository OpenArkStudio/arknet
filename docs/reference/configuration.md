# Build Options and Defaults

Use this page to look up build switches and public defaults. Follow
[installation](../get-started/installation.md) to configure a consuming application.

## CMake options

| Option | Default | Effect |
| --- | --- | --- |
| `ARKNET_USE_BOOST_ASIO` | `ON` | Select Boost.Asio and Boost.Beast |
| `ARKNET_ENABLE_SSL` | `OFF` | Enable TLS, HTTPS and WSS; link OpenSSL |
| `ARKNET_BUILD_TESTS` | `ON` at the top level, `OFF` when embedded | Build doctest and integration checks |
| `ARKNET_BUILD_EXAMPLES` | `ON` at the top level, `OFF` when embedded | Build example applications |
| `ARKNET_BUILD_BENCHMARKS` | `OFF` | Build the permanent benchmark executables |

## Direct-header macros

| Macro | Meaning |
| --- | --- |
| `ARKNET_USE_BOOST_ASIO=1` | Default `1` selects Boost; `0` selects Standalone Asio (TCP, UDP, RUDP, TLS) |
| `ARKNET_ENABLE_SSL` | Its presence enables secure transports; leave it undefined to disable them |
| `ARKNET_HEADER_ONLY` | Internal provider macro, not a switch for a compiled library |
| `ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR` | Invoke timer callbacks on errors; inspect `get_last_error()` |

Use identical provider, TLS and behavior macros in every translation unit.
CMake `ON`/`OFF` values and C++ macro-presence checks are different conventions.

## Runtime defaults

| Setting | Default | Details |
| --- | --- | --- |
| Application send queue | 16 MiB of payload and 1024 operations per object | [Ownership and backpressure](../concepts/sending.md) |
| Automatic reconnect | Enabled; one-second retry delay | [Reconnect](../guides/reconnect.md) |
| HTTP header / body limits | 8192 bytes / 16 MiB | [HTTP configuration](../features/http/configuration.md) |
| UDP client `SO_REUSEADDR` | Disabled | [UDP configuration](../features/udp/configuration.md) |
| TLS protocol / client verification | Minimum TLS 1.2; verify chain and DNS/IP identity | [TLS configuration](../features/tls/configuration.md) |
| Kernel socket buffers | Platform defaults unless explicitly configured | [Socket buffers](../guides/socket-buffers.md) |

Benchmark-requested socket sizes are measurement settings, not library defaults.
[Public API index](api.md) · [Errors](errors.md)
