# Redis Client and Connection Pool

The complete [client](../../../examples/redis_client.cpp) and [pool](../../../examples/redis_pool.cpp) examples connect only to the host and port supplied on the command line. They use `PING` and binary `ECHO` without modifying keys. Redis is a separate service, not an arknet server endpoint.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_EXAMPLES=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build build --target arknet_redis_client arknet_redis_pool --config Release

build/examples/arknet_redis_client 127.0.0.1 6379
build/examples/arknet_redis_pool 127.0.0.1 6379
```

For a multi-configuration generator, run the executables under `examples/Release/`. Standalone Asio uses `ARKNET_USE_BOOST_ASIO=OFF` and, if needed, `ARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`. The examples do not require a scripting runtime.

Set `ARKNET_REDIS_PASSWORD` in the process environment if authentication is enabled. ACL users can also set `ARKNET_REDIS_USERNAME`; no password is embedded in the program or accepted as a command-line argument. An explicit username with an empty password is supported when the server's ACL permits it.

## Single connection

Include `<arknet/redis/client.hpp>`. Configure `connect_options` and call `async_connect` before sending commands. `async_execute` submits one argument array; `async_pipeline` submits several and completes with ordered replies:

```cpp
const std::string payload("ark\0net", 7);
client.async_pipeline({{"PING"}, {"ECHO", payload}}, [](arknet::redis::result response) {
    if (!response)
    {
        // A sent command may have executed even when the operation failed.
        return;
    }
    // Inspect response.replies[i].type before reading its corresponding value.
});
```

Arguments are length-delimited `std::string` values, so embedded zeros and CR/LF do not split arguments. Reply values own their memory. Server `error` or `bulk_error` replies remain in `response.replies`; successful transport completion does not imply that every Redis command succeeded.

The complete example uses promises to wait on the main thread. Never wait on a future from an IO callback; callbacks run on the connection's IO lane. Keep application objects captured by callbacks alive through their completion.

## Exclusive pool leases

Include `<arknet/redis/pool.hpp>`. Configure `pool_options.connection`, then call `async_acquire`. A successful `acquire_result` contains a move-only `connection` lease. Submit commands through that lease, then call `release()` or let it leave scope. Use `discard()` when the connection must not be reused.

```cpp
pool.async_acquire([](arknet::redis::pool::acquire_result response) {
    if (!response)
    {
        return;
    }
    auto connection = std::move(response.connection);
    connection.async_execute({"PING"}, [](arknet::redis::result reply) {
        // Handle the reply; no blocking work on this callback thread.
    });
    // Leaving scope returns the lease after the pending command callback finishes.
});
```

Keep one lease for a complete `WATCH`/`MULTI`/`EXEC` sequence. Returning it waits for pending callbacks and ordered reply draining, then resets the connection and restores the pool's authentication, protocol and selected database before another borrower receives it. A failed or unsupported reset causes replacement, not reuse.

## TLS and mTLS

Both examples accept `HOST PORT --tls [CA_FILE]`:

```sh
build/examples/arknet_redis_client redis.example.com 6380 --tls /path/to/ca.pem
build/examples/arknet_redis_pool redis.example.com 6380 --tls /path/to/ca.pem
```

This selects `redis::tls_client` or `redis::tls_pool`. Certificate-chain and DNS/IP identity checks remain enabled. For mTLS, configure `tls_certificate_file` and `tls_private_key_file` in `client_options`; a pool uses `pool_options.client_configuration`. See [configuration](configuration.md#tls-options) for the exact fields.
