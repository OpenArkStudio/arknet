# HTTP and HTTPS Performance

[Introduction](../http.md) | [Usage](usage.md)

## Local Results

Release local loopback: HTTP/1.1 POST echoes a 1024-byte body, 16 clients,
window 16 and no extra application computation.

### HTTP

![HTTP throughput by IO model](../performance/assets/http-work0-throughput.svg)

![HTTP p99 RTT by IO model](../performance/assets/http-work0-latency.svg)

- Four contexts/one thread each has the highest throughput median and lowest
  p99 median for both backends.
- Moving from one context/two threads to one context/four threads increases
  throughput medians and p99 medians. The p99 run ranges overlap.

[Complete HTTP report](../performance/http.md)

### HTTPS

![HTTPS throughput by IO model](../performance/assets/https-work0-throughput.svg)

![HTTPS p99 RTT by IO model](../performance/assets/https-work0-latency.svg)

- Four contexts/one thread each has the highest throughput median and lowest
  p99 median for both backends.
- HTTPS has lower throughput medians than HTTP at the same load and thread
  model.

[Complete HTTPS report](../performance/https.md) ·
[Test environment](../performance/overview.md) · [Metrics and statistics](../testing.md)

[Payload, batch and execution comparisons](../performance/dimensions.md#http) ·
[HTTPS comparison](../performance/dimensions.md#https)

## Reproduce

Build from the repository root. Replace the Asio include path with
`-DARKNET_USE_BOOST_ASIO=ON` to test Boost.
With Visual Studio, add `--config Release` to the build command and run the
`.exe` under `benchmarks/Release/`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --target arknet_loopback_benchmark --parallel 3
for repetition in 1 2 3; do
  for protocol in http https; do
    build/benchmarks/arknet_loopback_benchmark \
      --protocol "$protocol" --certs tests/certs \
      --payload 1024 --clients 16 --window 16 \
      --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1 \
      > "build/${protocol}-${repetition}.json" || exit 1
  done
done
```

This loop compares HTTP and HTTPS with 1024-byte bodies, 16 clients, window 16
and one context/one thread over keep-alive connections. It collects three
fresh-process measurements per protocol, saving each result separately.
Change `--io-threads` to 2 or 4 for a shared context,
or use `--io-model sharded --io-threads 4` for four contexts/one thread each.
The complete shell matrix is in [Testing](../testing.md#performance-matrix).
HTTPS adds TLS to the same exchange. Each connection keeps 16 requests
outstanding, so RTT includes pipeline queueing. Test certificates are for local
loopback only.

Route handlers run on the session's serialized lane. Adding threads cannot
parallelize one connection's HTTP/1 response stream; see
[IO models](../threading.md) for workload choices.

The test includes parsing, serialization, send queues and scheduling but does
not isolate route dispatch or TLS handshake rate; repeat with the actual route
table and middleware. HTTP/2 and HTTP/3 remain [TODO](../roadmap.md), with no
corresponding performance results.
