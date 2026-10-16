# TCP connection load

English · [简体中文](CONNECTIONS_CN.md)

`arknet_connection_benchmark` runs a TCP server or client in a separate process. Use it to validate connection establishment, idle residency, closed-loop Echo traffic, resource limits and shutdown before running a deployment capacity test. A successful small loopback run does not establish a PCU limit or a production SLO.

The program uses arknet's public `tcp_server`, `tcp_client` and `io_pool`. It does not require Python, Ruby, a database or an extra runtime service. This version covers TCP callbacks, numeric IPv4/IPv6 addresses and the `idle` and `traffic` scenarios. TLS, reconnect workloads, fixed request arrival rates and real ark application messages are separate work.

## Build

With Boost 1.90 or newer available to CMake:

```sh
cmake -S . -B build/connections -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=OFF
cmake --build build/connections --target arknet_connection_benchmark --config Release
```

The executable is under `build/connections/benchmarks/`; a multi-configuration generator can add a `Release/` directory. Use a Visual Studio 2022 developer terminal on Windows. The normal arknet dependency/toolchain options also apply.

For Standalone Asio 1.38 or newer, additionally set `-DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`. The program includes only the TCP headers and does not require Boost.Beast.

It can also be compiled directly. For example, on Linux or macOS with Boost headers installed:

```sh
c++ -std=c++20 -O3 -DNDEBUG -pthread -DARKNET_USE_BOOST_ASIO \
  -Iinclude -I/path/to/boost benchmarks/connections.cpp -o connection-load
```

On Windows, CMake supplies the system networking and process-memory libraries. No performance run is required to build or consume arknet.

## Run the two roles

In the first terminal, start a server with an explicit total target:

```sh
./build/connections/benchmarks/arknet_connection_benchmark \
  --role server --scenario idle --listen 127.0.0.1 --port 19090 \
  --run-id 9001 --expected-connections 100 --max-connections 100 \
  --io-threads 2 --deadline-seconds 60
```

In another terminal:

```sh
./build/connections/benchmarks/arknet_connection_benchmark \
  --role client --scenario idle --target 127.0.0.1 --port 19090 \
  --run-id 9001 --client-id 1 --connections 100 --max-connections 100 \
  --connect-rate 100 --connect-parallel 16 --hold-seconds 10 \
  --io-threads 2 --deadline-seconds 45
```

Use `--scenario traffic` on both roles to send Echo requests. Set the client's `--payload 1024 --window 4` to allow up to four outstanding requests on each connection. Both roles must use the same scenario, run ID and payload size. `--payload` counts body bytes; each test frame adds a 64-byte control header and arknet's data-frame prefix.

For IPv6 loopback, replace both addresses with `::1`. For cross-machine tests, use the server's numeric interface address as the client target. Hostname resolution is intentionally outside this tool's measurement. A server may listen on a wildcard address; a client must target a concrete address. `--port 0` is supported on the server and reports the actual port in its first `ready` JSON line.

For multiple client processes, set the server's `--expected-connections` and `--max-connections` to the total of all client targets. Assign a distinct nonzero `--client-id` to each client process, including across machines. The default client ID is its PID, which is not globally unique across hosts. Connection IDs are local to the client process, beginning at 1. The server rejects duplicate client/connection pairs within a run.

## Stages and success

1. The client opens connections at no more than `--connect-rate`, with at most `--connect-parallel` TCP attempts pending. The configured rate is a ceiling, not a guaranteed achieved rate; scheduling and machine capacity can lower it.
2. Each connection exchanges `HELLO` and an identity acknowledgement. `established` counts successful TCP connects; `ready` counts completed application-level test handshakes. These are test-control frames, not ark authentication.
3. After all of that client process's connections are ready, it exchanges `START` acknowledgements and begins its hold period. `idle` sends no application heartbeat or business data during the hold. OS/library TCP keepalive is independent of this rule.
4. `traffic` fills a fixed per-connection request window. Each validated Echo response permits another request. This is closed-loop traffic; it does not claim a fixed arrival rate when the receiver slows down.
5. At the end of the hold, the client stops creating requests, drains outstanding Echo responses, exchanges `FIN` acknowledgements and closes all sockets. Both roles wait for their endpoints to stop before stopping and joining the IO pool.

The server's `--expected-connections` is independent of a single client's `--connections`. With a nonzero expected total, success requires that many admitted, ready, started, finished and disconnected connections, zero active sessions and zero pending sends. It can exit early once those conditions hold. With the default expected total of 0, the server observes until its deadline and returns `observation_complete`; this is not certification that a connection target was reached. `active_before_stop` records live connections at the observation boundary, before cleanup.

Each client process has its own hold boundary. This version does not synchronize the measurement phases of multiple client processes or machines. It also does not retry failed connects or replay requests.

## Bounds

| Option | Default | Meaning and accepted range |
| --- | --- | --- |
| `--run-id` | Required | Nonzero unsigned 64-bit identifier shared by the two roles. IDs are emitted as decimal JSON strings. |
| `--connections` | 1 | Client target, 1–`max-connections`. |
| `--expected-connections` | 0 | Server's explicit total target; 0 selects observation mode. Must not exceed `max-connections`. |
| `--max-connections` | 4096 | Maximum client target or cumulative server admissions during this run, up to 200,000. This limit is not a tested capacity. Closed sessions do not free additional admission slots. |
| `--connect-rate` | 100/s | Client launch-rate ceiling, 1–1,000,000/s. No catch-up burst after a scheduling delay. |
| `--connect-parallel` | 32 | Pending client TCP attempt limit, 1–4096. It does not limit already-connected sessions awaiting `HELLO`. |
| `--connect-timeout-ms` | 5000 | Per-attempt TCP deadline, 1–3,600,000 ms. |
| `--request-timeout-ms` | 5000 | Client `HELLO`, `START`, oldest outstanding Echo or `FIN` response deadline, 1–3,600,000 ms. The planned scan interval is at most 50 ms; OS scheduling and blocked output can delay execution further. |
| `--hold-seconds` | 1 | Positive client hold time; must be shorter than its total deadline. |
| `--deadline-seconds` | 30 | Positive overall run deadline, at most 86,400 s, covering ramp, handshakes, hold and protocol drain. For the server, it starts before listening. |
| `--cleanup-seconds` | 5 | Additional endpoint/pool cleanup grace, 1–60 s. |
| `--io-threads` | 1 | 1–64 library IO workers. The pool creates **N `io_context` objects, each run by one worker**. This is not one context shared by N workers. |
| `--payload` | 1024 | Echo body size, 1 B–1 MiB, excluding headers. Set the same value on both roles. |
| `--window` | 1 | Per-connection closed-loop request window, 1–1024. Used by the client in `traffic`. |
| `--max-buffer-bytes` | 4096 | Per-endpoint receive-buffer maximum, at least `payload + 64 + 9`, at most 16 MiB. Larger payloads require an explicit larger buffer. |
| `--max-inflight-bytes` | 64 MiB | Global pending-send payload-byte limit, at most 1 GiB. Client request windows must also fit within this limit. A limit rejection fails the run. |
| `--buffer-budget-bytes` | 512 MiB | Preflight budget for declared receive-buffer maxima, global pending-send capacity and client timestamp rings, up to 16 GiB. |
| `--sample-ms` | 1000 | Resource/progress output interval, 100–60,000 ms. |

The buffer preflight reservation is `N * max-buffer-bytes + max-inflight-bytes + timestamp ring storage`, where `N` is the client target or the server admission limit. This is a bound on the declared payload-buffer budget, **not an RSS limit**. Endpoint objects, allocator overhead, library queues/control structures, IO runtime and OS socket buffers require additional memory. Check the process and OS limits before increasing connection counts; this program does not raise FD limits or change system socket settings.

There is one main scheduling thread and one cleanup-watchdog thread in addition to the configured IO workers. It does not create a thread for each connection. Ctrl+C or SIGTERM requests a failed run with normal cleanup. If the run, final output or cleanup cannot finish by the overall deadline plus cleanup grace, the watchdog requests immediate OS process termination: `TerminateProcess` with code 124 on Windows, or `SIGKILL` on POSIX. POSIX shells commonly display 137 for signal 9, but it is a signal termination, not a portable exit-code guarantee. This path means `clean_shutdown=false`; no complete success summary is produced. Termination does not depend on another log record, a C-runtime flush or sanitizer exit handlers.

## Output and interpretation

Stdout is JSON Lines: a server `ready` event, periodic `sample` events and one final `result` event. Stderr contains argument/startup errors. Watchdog termination may leave output incomplete, so also record each process's exit code. Save each process's streams separately outside the Git repository.

| Result fields | Interpretation |
| --- | --- |
| `attempted`, `established`, `connect_failures` | Client TCP attempts and results. A connection failure stops new admission; a failed run may attempt fewer connections than requested. |
| `accepted`, `admitted`, `ready`, `started`, `finished`, `disconnected` | Server's successful native accepts, capacity-admitted sessions and test-protocol stages. Client fields describe its corresponding stages. |
| `active_before_stop`, `active`, `peak_active` | Live sockets at the observation boundary, final live count after cleanup, and process peak. |
| `data_sent`, `data_received`, `outstanding_requests` | Echo data counts; control frames are excluded. Successful traffic drains all requests. |
| `traffic_and_drain_elapsed_s` | Client: local hold start through its last `FIN` acknowledgement. Server: first `START` through its last successfully written `FIN` acknowledgement. These scopes differ and are not a cross-host synchronized window. |
| `echo_messages_per_second`, `echo_payload_bytes_per_second` | Received Echo messages/body bytes divided by that role's traffic-and-drain interval. Includes drain and, on the server, staggered client starts. Excludes protocol/TCP/IP wire overhead. |
| `rtt_p50_upper_us`, `rtt_p95_upper_us`, `rtt_p99_upper_us` | Client Echo round-trip bucket upper bounds in microseconds, including generation, validation, queues and transport. Null when no Echo observations exist. No one-way or real-business latency is measured. |
| `rtt_buckets`, `latency_observations` | All observed RTTs retained as 513 fixed counters; memory does not grow with request count. |
| `cpu_percent`, `peak_rss_bytes` | This process's user+system CPU and lifetime RSS high-water mark. CPU 100% means one logical core; it may exceed 100%. Sample CPU covers the preceding sample interval; final CPU covers the entire run including ramp and cleanup. It excludes the other process. Missing OS metrics are null. |
| `content_errors`, `sequence_errors`, `send_errors`, `timeouts`, `resource_rejections` | Validation, transport, deadline or configured-budget failures. `shutdown_cancelled_sends` is separate from normal send errors. |
| `clean_shutdown`, `outcome`, `first_error` | Cleanup status and `target_complete`, `observation_complete` or `failed`. Clean shutdown alone does not imply delivery success or a target was reached. |

Bucket 0 contains 0 µs. For positive integer microseconds, the remaining buckets use eight subdivisions per power of two; `connections_protocol.hpp` defines exact indices and inclusive upper bounds. The width is at most 12.5% of that power-of-two base, except the 1 µs integer resolution at small values. Matching bucket counts can be added; per-process p99 values cannot be averaged or added to obtain a global p99. Merge only results from the same coordinated run and intended measurement phases. Raw RTT observations are not stored.

Normal exit codes: 0 for completed target/observation with clean shutdown, 1 for a runtime/validation/deadline failure, and 2 for invalid arguments or startup exceptions. Watchdog termination is code 124 on Windows and signal 9 on POSIX; native supervisors should inspect the signal status, while shells commonly show 137. Failed runs are evidence to diagnose, not throughput results to silently discard or retry over.

## Small correctness checks

Configure with `-DARKNET_BUILD_TESTS=ON` to also build `arknet_connection_protocol_test`. It checks identity preservation, corrupted/truncated frame rejection, overflow and mergeable RTT bucket bounds. `arknet_check` includes those checks and `--help`; the POSIX full-output-pipe process check verifies watchdog termination without relying on output consumption. Deployment load runs remain local/manual.

Before increasing scale, run both idle and traffic with a few connections, IPv4 and IPv6, and inspect both final results. Also verify a refused connection, mismatched run ID, target deadline, duplicate identity and an invalid buffer/window budget fail with a nonzero exit. Keep the command lines, executable/source identity, machine configuration, all failures and both process logs with each measurement. This tool's test protocol does not replace an ark application, authentication, reconnect recovery or cross-machine production acceptance.
