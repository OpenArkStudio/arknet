# TCP Performance

## Local Results

Release local loopback: 1024-byte messages, 16 clients, window 16 and no extra
application computation.

![TCP throughput by IO model](../performance/assets/tcp-work0-throughput.svg)

![TCP p99 RTT by IO model](../performance/assets/tcp-work0-latency.svg)

### What This Run Shows

- Four contexts/one thread each has the highest throughput median for both
  backends.
- The lowest p99 median belongs to one context/two threads for standalone and
  four contexts/one thread each for Boost.
- With one client and window 1, one context/four threads has lower throughput
  medians than one context/one thread for both backends.

[Complete TCP report](../performance/tcp.md) ·
[Test environment](../performance/overview.md) ·
[Metrics and statistics](../testing.md)

[Payload, batch and execution comparisons](../performance/dimensions.md#tcp)

## Build and Compare IO Models

Run from the repository root with a Release build. Replace the Asio include path
with `-DARKNET_USE_BOOST_ASIO=ON` for the Boost backend, keeping other settings
and hardware unchanged.

Commands assume a single-configuration generator. With Visual Studio, add
`--config Release` to the build command and run the executable under
`benchmarks/Release/`. Windows executables end in `.exe`.

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf --target arknet_loopback_benchmark arknet_coroutine_benchmark --parallel 3
build-perf/benchmarks/arknet_loopback_benchmark \
  --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
```

This single configuration uses 1024-byte messages, 16 clients, window 16 and
one context/one thread. It writes one JSON result to standard output. Change
`--io-threads` to 2 or 4 for a shared context, or use `--io-model sharded
--io-threads 4` for four contexts/one thread each. `--work 10000` adds 10,000
deterministic iterations per server echo; `0` adds no application computation.
These iterations do not represent a real parser or application. Repeat each
configuration three times in fresh processes; the complete shell matrix is in
[Testing](../testing.md#performance-matrix).

## Native Asio Coroutine Scheduler

```sh
build-perf/benchmarks/arknet_coroutine_benchmark \
  --execution coroutine --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
```

The Asio coroutine benchmark program uses `awaitable`/`co_spawn` and fixed-size
TCP batches; see the [coroutine report](../performance/coroutine.md) and
[IO models](../threading.md). A public arknet coroutine endpoint API is deferred.
With window greater than one it reports batch RTT, unlike the callback program's
message RTT, framing and replenishment rules; compare thread models within each
program. `--execution callback` runs the matched native callback mode; both
native modes use the same batch protocol.

The TCP callback test uses `use_dgram` framing and does not cover unframed
streams, connection establishment rate or real request handlers. Short loopback
runs do not establish cross-host capacity, WAN latency or endurance.

See [testing and report metadata](../testing.md),
[callback benchmark source](https://github.com/OpenArkStudio/arknet/blob/main/benchmarks/loopback.cpp)
and [coroutine source](https://github.com/OpenArkStudio/arknet/blob/main/benchmarks/coroutine.cpp).
