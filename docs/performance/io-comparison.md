# IO Models and Coroutine Performance

## Native Asio Coroutine Baseline

Native Asio TCP coroutine Release local loopback: 1024-byte messages,
16 clients and window 16. Latency is batch RTT.

### No Extra Application Computation

![Coroutine throughput by IO model](assets/coroutine-work0-throughput.svg)

![Native Asio coroutine batch p99 RTT by IO model](assets/coroutine-work0-latency.svg)

- One context/four threads has the highest throughput median and one
  context/two threads the lowest batch p99 median for both dependency combinations.
- Standalone's batch p99 run ranges overlap between one context/two threads
  and one context/four threads, so these runs do not establish a consistent
  tail-latency advantage.

### 10,000 Deterministic Iterations Per Echo

![Native Asio coroutine throughput with application computation](assets/coroutine-work10000-throughput.svg)

![Native Asio coroutine batch p99 RTT with application computation](assets/coroutine-work10000-latency.svg)

- Four contexts/one thread each has the highest throughput median and lowest
  batch p99 median for both dependency combinations.
- Batch p99 run ranges overlap between one context/four threads and four
  contexts/one thread each for both dependency combinations.

[Complete coroutine baseline](coroutine.md) ·
[Payload, batch and execution comparisons](dimensions.md#native-tcp)

[Test environment](overview.md) · [Metrics and statistics](methodology.md) ·
[TCP callback results](tcp.md)

## Reproducing the Comparison

The checked-in programs `benchmarks/loopback.cpp` (arknet callbacks) and
`benchmarks/coroutine.cpp` (native Asio callbacks/coroutines) each have an
independent `main`. Build with CMake and run the C++ executable directly:

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf --target arknet_coroutine_benchmark --parallel 3
build-perf/benchmarks/arknet_coroutine_benchmark \
  --execution coroutine --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --work 0 --warmup 0.25 --seconds 1
```

This example uses one context/four threads, 1024-byte messages, 16 clients and
window 16, writing JSON to standard output. Replace the Asio include argument
with `-DARKNET_USE_BOOST_ASIO=ON` for Boost. With Visual Studio, add
`--config Release` and run the `.exe` under `benchmarks/Release/`.

The `--io-model` values `shared` and `sharded` select one context with multiple
threads and one context per thread, respectively. Set `--io-threads 1` to test
one context/one thread. `--execution callback` runs the matched native callback
mode. `--work 10000` adds 10,000
deterministic iterations per server echo; `0` adds no application computation.
This is a synthetic load, not a production parser or crypto algorithm. Compare
topologies within the same benchmark program, dependency combination, payload, concurrency and
computation setting. The Asio coroutine program sends and reads fixed-size batches:
with `--window` above one, its latency is batch RTT. The arknet callback program continuously
replenishes its window and reports individual-message RTT. Their throughput and
latency do not establish a coroutine-versus-callback speedup.

The expanded comparison runs callback and coroutine modes of the same native
Asio TCP program. Its server reads a complete batch, performs the configured
computation on each message, then writes the full reply. Window 1 measures
message RTT; larger windows measure batch RTT. See the
[matched comparison](dimensions.md#native-tcp).

Repeat configurations in fresh C++ processes, or use the maintainer's shell
matrix in [Testing](methodology.md#performance-matrix). Only that batch tool needs
`jq`, GNU `timeout` and `shasum`; direct C++ tests and single benchmarks do not.

Raw results and test commands are documented in [Testing and Performance](methodology.md).
Short loopback measurements cannot predict WAN latency or production capacity.
