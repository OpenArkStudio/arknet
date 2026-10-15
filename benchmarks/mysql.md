# MySQL client benchmark

Compile the standalone C++ executable and run it against a MySQL server. It only
selects a constant binary value; it does not create tables or write application data.

```sh
cmake -S . -B build-mysql -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON \
  -DARKNET_BUILD_BENCHMARKS=ON
cmake --build build-mysql --target arknet_mysql_benchmark --parallel 3
export ARKNET_MYSQL_PASSWORD='your-test-password'
./build-mysql/benchmarks/arknet_mysql_benchmark \
  --host db.example.com --port 3306 --username benchmark --database benchmark \
  --ca /path/to/ca.pem --mode prepared --clients 4 --window 16 \
  --payload 4096 --rounds 3 --seconds 5 --warmup 1
unset ARKNET_MYSQL_PASSWORD
```

Use `--plain` instead of `--ca` only for a deliberately unencrypted test connection.
The executable still needs OpenSSL for authentication. Authentication plugins that
require a full password exchange may refuse an unencrypted connection.

Set `ARKNET_USE_BOOST_ASIO=OFF` and `ARKNET_ASIO_INCLUDE_DIR` to compile with
Standalone Asio. No Python or Ruby launcher is required. Shell loops can run a matrix;
keep raw JSON lines outside the source tree.

## Workload and dimensions

| Option | Meaning | Default |
| --- | --- | --- |
| `--mode text` | `COM_QUERY` with `SELECT X'…'`; each payload byte is encoded as two hex digits | — |
| `--mode prepared` | Prepare `SELECT ?` once per connection, then execute with a binary string parameter | `prepared` |
| `--clients` | Concurrent connections, each owning one IO context and worker thread | 4 |
| `--window` | Outstanding queries per connection, including the active command and its FIFO queue | 1 |
| `--payload` | Bytes in the selected value; every response is checked byte for byte | 1024 |
| `--rounds` | Measurement rounds on the same connections after warmup | 3 |
| `--seconds` | Target duration per round, followed by completion of outstanding queries | 1 |
| `--warmup` | Warmup duration before the measured rounds | 0.25 |

MySQL executes one command at a time on each connection. Increasing `--window`
adds client queueing; it does not enable concurrent statements on one socket.
Neither mode uses coroutines or measures the connection pool. Text and prepared
modes send different request representations, so compare the complete execution
paths rather than interpreting the difference as a codec-only cost.

## Output

The program buffers all measured rounds and emits one JSON line per round only
after every round succeeds. A connection, warmup, response validation
or measurement failure makes the program exit nonzero. Discard **all** output from
an unsuccessful invocation, including any preceding successful rounds.

| Metric | Meaning |
| --- | --- |
| `queries_per_second` | Verified successful queries divided by measured wall time, including final drain |
| `query_p50_us`, `query_p99_us` | Submission-to-completion latency, including client queueing; weighted per-chain reservoir estimate |
| `client_cpu_percent` | Client process CPU usage; one fully occupied core is 100%, multiple cores can exceed 100% |
| `process_peak_rss_bytes` | Process-lifetime peak resident memory; not an interval allocation or per-connection measurement |
| `failed_queries` | Failed or mismatched responses; a valid invocation has zero |

At most about 65,536 latency samples are retained across the request chains.
Connection establishment, authentication and statement preparation happen before
warmup and are excluded from the measurements. Server CPU is excluded. Record the
server version/configuration, client/server machines, compiler, dependency versions,
TLS setting and network path alongside any published results. Loopback measurements
do not establish remote deployment capacity.
