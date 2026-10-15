# Examples

The repository includes small C++ applications, and each protocol guide provides
complete client/server programs. Database guides cover clients and pools.
Use the same dependency combination and TLS settings for transport peers.

## Repository applications

| Program | Source | Run |
| --- | --- | --- |
| TCP echo server | [echo_server.cpp](../../examples/echo_server.cpp) | `build/examples/arknet_echo_server` |
| HTTP echo server | [http_echo_server.cpp](../../examples/http_echo_server.cpp) | `build/examples/arknet_http_echo_server` |
| IPv6 client/server | [ipv6_echo.cpp](../../examples/ipv6_echo.cpp) | `build/examples/arknet_ipv6_echo` |
| MySQL client | [mysql_client.cpp](../../examples/mysql_client.cpp) | `build/examples/arknet_mysql_client` |
| MySQL pool | [mysql_pool.cpp](../../examples/mysql_pool.cpp) | `build/examples/arknet_mysql_pool` |
| Redis client | [redis_client.cpp](../../examples/redis_client.cpp) | `build/examples/arknet_redis_client` |
| Redis pool | [redis_pool.cpp](../../examples/redis_pool.cpp) | `build/examples/arknet_redis_pool` |

Build from the repository root:

```sh
cmake -S . -B build -DARKNET_BUILD_EXAMPLES=ON \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
cmake --build build --parallel 3
```

For Visual Studio, add `--config Release` and run the `.exe` from `examples/Release/`.
Read each program's source for its port and shutdown behavior.
Database examples take an explicit endpoint and a password environment variable;
see [MySQL](../features/mysql/usage.md) and [Redis](../features/redis/usage.md).

## Protocol pairs

| Protocol | Server | Client |
| --- | --- | --- |
| TCP | [Program](../features/tcp/server.md) | [Program](../features/tcp/client.md) |
| UDP | [Program](../features/udp/server.md) | [Program](../features/udp/client.md) |
| WebSocket | [Program](../features/websocket/server.md) | [Program](../features/websocket/client.md) |
| HTTP | [Program](../features/http/server.md) | [Program](../features/http/client.md) |
| TLS TCP | [Program](../features/tls/server.md) | [Program](../features/tls/client.md) |

[HTTPS](../features/http/configuration.md) and [WSS](../features/websocket/configuration.md)
describe adaptations of the complete programs. Secure examples require OpenSSL
and `ARKNET_ENABLE_SSL=ON`; repository private keys are for local tests only.

Performance executables are under `benchmarks/`; see [measurement methodology](../performance/methodology.md).
