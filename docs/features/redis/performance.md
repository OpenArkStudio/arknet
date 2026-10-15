# Redis Performance

No measured Redis performance report is published yet. The independent C++ benchmark is part of the repository; performance claims require a complete local measurement matrix and successful correctness and shutdown checks.

## Reproduce

Run a separate Redis server, build the benchmark in Release mode, then supply its endpoint:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build build --target arknet_redis_benchmark --config Release

build/benchmarks/arknet_redis_benchmark --host 127.0.0.1 --port 6379 \
  --clients 4 --payload 1024 --pipeline 16 --window 1 \
  --warmup 0.5 --seconds 3 --rounds 3
```

Authentication uses `ARKNET_REDIS_PASSWORD` and optional `--username`. For TLS, specify the TLS port and add `--tls --ca /path/to/ca.pem`. [Full benchmark usage](../../../benchmarks/redis.md) covers options, limits and output. No Python or Ruby entry point is needed.

## Workload and comparisons

The workload submits binary `ECHO` commands and validates every reply's type, count and contents. Each connection owns one IO context and thread. Compare at least:

| Dimension | Values |
| --- | --- |
| Payload | 1, 4 and 16 KiB |
| Commands per pipeline batch | 1, 4, 16 and 64 |
| Concurrent batches per connection | 1 and 4 |
| Repetition | At least 3 rounds for every combination |
| Dependency and transport | Boost.Asio / Standalone Asio; TCP / TLS |

Keep connection counts, server configuration and machine conditions fixed across comparable groups. Record client and server hardware, OS, compiler/build mode, Redis, Asio and OpenSSL versions, warmup and measurement durations. Client and Redis server sharing one machine affects CPU scheduling and does not establish cross-machine capacity.

## Metrics

| Metric | Unit and interpretation |
| --- | --- |
| Command throughput | Verified successful commands per second |
| Batch RT p50 / p99 | Microseconds from submission to the complete pipeline reply, including client queueing; not per-command latency |
| Client CPU | Percentage of one core; 100% means one fully occupied core, excluding the Redis process |
| Peak RSS | Client process lifetime resident-memory peak in bytes, including warmup and preceding rounds |

After all rounds succeed, the program emits one JSON line per recorded round. Connection, warmup or reply verification errors produce a failure exit and no JSON rows. Latency sampling is globally bounded and weighted by completed batch counts; short rounds with few samples cannot characterize rare delays. This benchmark covers the callback connection API, not coroutine scheduling or connection-pool contention.
