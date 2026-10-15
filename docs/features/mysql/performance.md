# MySQL Performance and Validation

A measured MySQL performance report is pending. Functional test counts, protocol parser throughput and successful authentication do not establish query throughput or deployment capacity. No latency, CPU or capacity figures are published for this module yet.

## Run the benchmark

The formal C++ program `arknet_mysql_benchmark` performs read-only text or prepared SELECT queries and verifies binary replies. Build and run it against a separately configured endpoint:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_USE_BOOST_ASIO=ON
cmake --build build --target arknet_mysql_benchmark --config Release
build/benchmarks/arknet_mysql_benchmark --host mysql.example.com --port 3306 \
  --username application_user --ca /path/to/ca.pem --mode prepared \
  --clients 4 --window 4 --payload 1024 --warmup 0.5 --seconds 3 --rounds 3
```

Supply the password through `ARKNET_MYSQL_PASSWORD`. TLS and certificate verification are enabled by default. [Full benchmark usage](../../../benchmarks/mysql.md) covers modes, bounds and output metrics. The executable is the test entry point; no Python or Ruby launcher is required.

## Functional validation

Validation includes an isolated MySQL Community Server 8.4.11 instance and controlled C++ protocol peers. Coverage includes verified TLS, password rejection, text queries, binary prepared parameters, transactions and session reset. Protocol tests exercise fragmentation, packet sequencing, the `0xffffff` physical-packet boundary, typed values, queue/result limits, cancellation and callback destruction.

Boost.Asio and Standalone Asio builds, plus ASan/UBSan runs, check correctness and memory behavior. These checks are separate from performance measurements. See [the complete runnable example](usage.md) to verify connectivity and API behavior against your endpoint.

## Measurement matrix

A local query benchmark should record the following dimensions before drawing performance conclusions:

| Dimension | Comparison |
| --- | --- |
| Operation | The provided benchmark compares text SELECT and prepared SELECT; writes and transactions require a separate workload |
| Payload | 1, 4 and 16 KiB result or parameter values, with row counts held constant |
| Queue depth | 1, 4, 16 and 64 outstanding requests per connection |
| Connections | Fixed connection counts, reported with the actual IO context and thread count |
| Repetition | At least 3 independent rounds per combination after warmup |
| Transport and dependency | Verified TLS; Boost.Asio and Standalone Asio |

Each bare connection executes commands sequentially. Increasing its queued depth adds outstanding application work; it does not multiplex simultaneous commands on the MySQL wire. Transaction benchmarks must identify the commands included per transaction and verify committed data.

## Report units and environment

Report successful operations per second, operation RT p50/p99 in microseconds including queueing, errors, client CPU as a percentage of one core, and client peak RSS in bytes. Distinguish queries, returned rows and committed transactions; they are different counts. Report server CPU separately if measured.

The report must identify client and server hardware, OS, compiler and Release flags, server version/configuration, dependency versions, TLS settings, payload/row/queue/connection counts, warmup, measurement duration and repetition count. Client and server sharing a machine affects scheduling. Capacity conclusions require the intended deployment topology and workload, beyond local loopback.

Performance data and charts belong to completed local measurements. Sanitizer runs and CI correctness checks do not supply those figures.

The current benchmark uses independent connections. Pool acquisition contention, return/reset and database-restoration overhead need a separate pool workload.
