# WebSocket Performance

## Local Results

Release local loopback: 1024-byte binary messages, 16 clients, window 16 and
no extra application computation.

### WebSocket

![WebSocket throughput by IO model](../performance/assets/websocket-work0-throughput.svg)

![WebSocket p99 RTT by IO model](../performance/assets/websocket-work0-latency.svg)

- Four contexts/one thread each has the highest throughput median for both
  backends.
- Standalone's lowest p99 median belongs to four contexts/one thread each;
  Boost's belongs to two contexts/one thread each, whose run range overlaps
  four contexts/one thread each.
- With one client and window 1, one context/four threads has lower throughput
  medians than one context/one thread for both backends.

### WSS

![WSS throughput by IO model](../performance/assets/wss-work0-throughput.svg)

![WSS p99 RTT by IO model](../performance/assets/wss-work0-latency.svg)

- Four contexts/one thread each has the highest throughput median and lowest
  p99 median for both backends.
- WSS has lower throughput medians than plaintext WebSocket at the same load
  and thread model.

[Complete WebSocket report](../performance/websocket.md) · [Complete WSS report](../performance/wss.md)

[Test environment](../performance/overview.md) ·
[Metrics and statistics](../testing.md)

[Payload, batch and execution comparisons](../performance/dimensions.md#websocket) ·
[WSS comparison](../performance/dimensions.md#wss)

## Reproduce the IO Comparison

Run from the repository root. For Boost, replace the standalone include argument
with `-DARKNET_USE_BOOST_ASIO=ON` and keep the other settings unchanged.

Commands assume a single-configuration generator. With Visual Studio, add
`--config Release` to the build command and run the executable under
`benchmarks/Release/`. Windows executables end in `.exe`.

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf --target arknet_loopback_benchmark --parallel 3
build-perf/benchmarks/arknet_loopback_benchmark \
  --protocol websocket --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
```

This configuration uses 1024-byte binary messages, 16 clients, window 16 and
one context/one thread, writing JSON to standard output. Repeat each
configuration three times in fresh processes. Change `--io-threads` to 2 or 4
for a shared context, or use `--io-model sharded --io-threads 4` for four
contexts/one thread each. `--work 10000` adds 10,000 deterministic iterations
per server echo; `0` adds no application computation. The complete shell matrix
is in [Testing](../testing.md#performance-matrix). One connection remains
serialized; see [IO models](../threading.md) for workload choices.

## Enable TLS

Use `--protocol wss --certs tests/certs` in a TLS-enabled build.
WSS verifies test certificates; see the [WSS report](../performance/wss.md) for
results and [TLS performance](../tls/performance.md) for the build commands.

This test measures binary echoes after upgrade, excluding browser scheduling,
upgrade rate, idle connections, compression, ping/pong, authentication, fan-out
and slow peers. The backends use different Asio/Beast versions, so differences
cannot be attributed solely to backend selection.

See [testing](../testing.md),
[WebSocket tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/websocket.cpp)
and [benchmark source](https://github.com/OpenArkStudio/arknet/blob/main/benchmarks/loopback.cpp).
