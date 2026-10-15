# Redis benchmark

The compiled `arknet_redis_benchmark` measures the native Redis client against a separately running Redis server. Every command is a binary `ECHO`; the program verifies the type, count, and exact contents of every reply. The payload includes zero bytes and all byte values when its size is at least 256 bytes. It does not change Redis keys.

## Build and run

```sh
cmake -S . -B build/redis-benchmark -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build build/redis-benchmark --target arknet_redis_benchmark --config Release

./build/redis-benchmark/benchmarks/arknet_redis_benchmark \
  --host 127.0.0.1 --port 6379 --clients 4 \
  --pipeline 16 --window 1 --payload 1024 \
  --warmup 0.5 --seconds 3 --rounds 3
```

With a multi-configuration generator, the executable is under `benchmarks/Release/`. To use Standalone Asio, configure `ARKNET_USE_BOOST_ASIO=OFF`; set `ARKNET_ASIO_INCLUDE_DIR` if its headers are not on the default search path. No Python or Ruby launcher is required.

Set the `ARKNET_REDIS_PASSWORD` environment variable when authentication is required; the program does not accept or print a password argument. Add `--username USER` for an ACL user. For TLS, add `--tls --ca /path/to/ca.pem` and use the TLS port. Certificate and hostname verification remain enabled. Omitting `--ca` uses the client's default trust configuration.

## Workload dimensions

| Option | Default | Meaning |
| --- | --- | --- |
| `--clients` | `4` | Connections; each has one library-owned IO context and thread, maximum 128 |
| `--pipeline` | `16` | Commands submitted together in one pipeline batch, maximum 1024 |
| `--window` | `1` | Concurrent pipeline batches per connection, maximum 64 |
| `--payload` | `1024` | Bytes echoed by each command, from 0 to 1 MiB |
| `--warmup` | `0.25` | Seconds of verified load before recorded rounds; 0 disables warmup |
| `--seconds` | `1` | Target duration of each recorded round |
| `--rounds` | `3` | Independent recorded rounds, maximum 100 |

Each connection has at most `pipeline × window` commands in flight. Payload plus a conservative per-command overhead must fit the 64 MiB per-connection budget; larger combinations are rejected before connecting. The last batch is drained before a round ends, so `elapsed_seconds` may exceed `--seconds`.

For comparisons, vary payloads (for example 1024, 4096, and 16384 bytes), batch sizes (1, 4, 16, and 64), and windows (1 and 4). Repeat each combination with `--rounds 3` or more. Keep the client/server machines, Redis configuration, compiler, dependency versions, connection count, and transport unchanged unless they are the dimension under comparison. Run Release builds on an otherwise idle machine; do not compare sanitizer builds or CI hosts with local performance results.

## Output

After warmup and all recorded rounds succeed, the program writes one JSON object per round to standard output. Diagnostics go to standard error. A connection, warmup, verification, or measurement failure produces a nonzero exit status and no JSON rows, including when an earlier round succeeded.

| Metric | Meaning |
| --- | --- |
| `commands_per_second` | Successfully verified commands divided by actual elapsed time |
| `batch_roundtrip_p50_us`, `batch_roundtrip_p99_us` | Complete pipeline batch turnaround, in microseconds, including client queueing; **not individual command latency** |
| `client_cpu_percent_of_one_core` | Client process CPU time divided by elapsed time, multiplied by 100; one fully occupied core is 100%, so multiple threads can exceed 100% |
| `process_peak_rss_bytes` | Client process peak resident memory since process start, including warmup and prior rounds; not the memory increase of this round |
| `successful_commands`, `failed_commands` | Commands counted after complete batch verification; a failed batch counts all its commands as failed |
| `latency_sample_count` | Number of batch latency samples retained across all concurrent chains, at most 65536 |

The CPU and memory metrics exclude the external Redis server. Unavailable platform metrics are `null`. Latency uses a bounded reservoir per concurrent chain; combined quantiles weight each sample by that chain's successful batch count divided by its retained sample count. Quantiles are estimates when sampling is active. Inspect the sample count before interpreting a tail percentile; a short round with few completed batches cannot characterize rare delays.

This tool measures the callback API and the one-context/one-thread-per-connection configuration. It does not establish results for coroutine APIs, shared IO contexts, connection pools, or production deployment capacity.

## Generate reports

The optional Node tool `benchmarks/report_database_clients.mjs` converts completed JSONL results into bilingual Markdown and SVG charts. Node is needed for this reporting step, not for the benchmark executable. Keep raw data outside the repository and provide measurement metadata, expected group counts and measured source hashes; `--help` lists the fields.

```sh
node benchmarks/report_database_clients.mjs --input /tmp/redis-rounds \
  --metadata /tmp/redis-measurement.json --out-dir /tmp/database-report
node --test benchmarks/report_database_clients.test.mjs
```

The output follows the documentation layout: `features/redis/performance*.md` and `performance/assets/redis-*.svg`. A failed or incomplete matrix is rejected before report files are written. Bars show the median of round measurements; error bars span the minimum and maximum. RT p50/p99 values are summarized across round percentiles, not pooled into a new percentile.
