# Testing and Measurement

[Local performance report](performance/overview.md)

Functional tests establish correctness; sustained stress checks repeated work,
restarts and cleanup. Performance measurements run locally after those checks
pass and use a Release build without sanitizers. CI checks regressions and
smoke execution; its shared-runner timing is not the published baseline.

## Build and Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
cmake --build build --target arknet_check
build/tests/arknet_stress_test --protocol all --seconds 60 \
  --clients 16 --rounds 2 --window 4 --certs tests/certs
```

For Boost, replace the Asio include argument with
`-DARKNET_USE_BOOST_ASIO=ON`. Windows executables have the `.exe` suffix.
The repository's TLS certificates are for testing, not production identities.

Tests use doctest 2.5.3, stored under `tests/vendor/doctest`; the framework is
not installed or linked by library consumers. `arknet_check` runs the test
executables, install-consumer check and enabled benchmark smoke cases directly,
with timeouts and logs in `build/tests/logs/`. It does not use CTest.

```sh
build/tests/arknet_tcp_test --list-test-cases
build/tests/arknet_tcp_test --test-case="*framing*"
build/tests/arknet_tls_test --reporters=junit --out=build/tls.xml
```

The functional suites and benchmarks are independent C++ programs with their
own `main`; compile and run them directly. Python, Ruby, `jq`, GNU `timeout`
and `shasum` are not prerequisites for these C++ tests or single benchmarks.

### Single Benchmark

```sh
build/benchmarks/arknet_loopback_benchmark \
  --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
build/benchmarks/arknet_coroutine_benchmark \
  --execution coroutine --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --work 0 --warmup 0.25 --seconds 1
```

Each executable writes one JSON result to standard output and returns nonzero
on failure. Repeat a configuration in fresh processes with a short shell loop:

```sh
for repetition in 1 2 3; do
  build/benchmarks/arknet_loopback_benchmark \
    --protocol tcp --payload 1024 --clients 16 --window 16 \
    --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1 \
    > "build/tcp-${repetition}.json" || exit 1
done
```

## Correctness Coverage

| Suite | Coverage |
| --- | --- |
| TCP / UDP / WebSocket | Framing and binary/empty data, custom sessions, endpoint routing, reconnect and owned sends |
| HTTP / router | Raw-peer chunked/HEAD/Expect semantics, ordered pipelines, limits, method/path dispatch, middleware, errors and HTTP/HTTPS binding |
| Lifecycle / detail / IO pool | Timer/post cancellation, real completion before destruction, mutable listeners, executor serialization, queued work and restart |
| Concurrency / keepalive | Send limits, callback completion accounting, concurrent shutdown, TCP keepalive options |
| TLS | Trust/identity rejection, mTLS, deadlines, WSS and fresh stream on reconnect |
| IPv6 | TCP, UDP/session/cast, WS/WSS, HTTP/HTTPS, TLS IP identity and handshake Host |
| Installed consumer | Provider/TLS configuration propagation and installed public headers |

Sustained stress validates content/sequence, completion counts, connection and
server restarts, pending writes and queue cleanup. Each protocol splits
`--seconds` across the built-in scheduler, one context/one thread, one context/four
threads and four contexts/one thread each,
then across the requested restart rounds. Seven protocols with
`--seconds 60` therefore request seven minutes of traffic in total, plus setup
and teardown. Slow receivers test backpressure rather than throughput.

## Local Verification

The current verification host is a Mac mini `Mac16,11`, Apple M4 Pro,
14 physical/logical cores (10 performance and 4 efficiency cores), 48 GiB RAM,
macOS 27.0.1 (26A434), Apple Clang
21.0.0, CMake 4.3.3, standalone Asio 1.38.2, Boost 1.90.0 and OpenSSL 3.6.4.
Complete checks on 2026-10-10 passed as follows. Normal builds include benchmark
smoke cases and the installed consumer; sanitizer builds disable benchmarks.
Checks count programs invoked by `arknet_check`; cases count doctest cases.

| Backend | Build | TLS | Checks / cases | Result and log |
| --- | --- | --- | ---: | --- |
| standalone | Release | ON | 40 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone.log) |
| standalone | Release | OFF | 30 / 87 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-notls.log) |
| standalone | Debug | ON | 40 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-debug-tls.log) |
| standalone | Debug | OFF | 30 / 87 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-debug-notls.log) |
| Boost | Release | ON | 40 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost.log) |
| Boost | Release | OFF | 30 / 87 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-notls.log) |
| Boost | Debug | ON | 40 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-debug-tls.log) |
| Boost | Debug | OFF | 30 / 87 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-debug-notls.log) |
| standalone | Debug + ASan/UBSan | ON | 15 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-asan.log) |
| standalone | Debug + TSan | ON | 15 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-tsan.log) |

After adding IPv6, Release + TLS complete checks passed again for both backends,
each with **41 checks and 128 cases**:
[standalone log](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/final-standalone-tls.log),
[Boost log](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/final-boost-tls.log).
Separate IPv6 checks cover both backends with TLS on and off; ASan/UBSan, TSan
and 30 short benchmark probes also passed. See the
[IPv6 check record](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/ipv6/README.md).
These probes validate options, protocols and CPU percentage output rather than performance comparisons.

After the future-lifetime, MSVC matcher, UDP completion and queued-cancellation fixes,
and the clang-format 18 style pass, the complete local checks passed
with **41 checks / 133 cases** per Release backend and **16 checks / 133 cases**
per standalone sanitizer build. GCC/libstdc++ regression checks also passed.
See the [portability check record](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/portability/README.md).

Shutdown regressions cover sessions/strands retained across contexts and cleanup
after coroutine connection failures. Historical diagnostics remain under
`benchmarks/results/diagnostics/`; published performance reports use only complete
measurements collected again after the shutdown fix.

ASan/UBSan and TSan are available on this Mac and run separately from
performance measurements. LeakSanitizer support differs by platform; a
passing Apple ASan run is not a Linux leak-check result.

### Sanitizer Checks

Sanitizers instrument a development build and report errors observed while tests run.

| Tool | Checks |
| --- | --- |
| ASan (AddressSanitizer) | Memory errors such as out-of-bounds access, use-after-free and double-free |
| TSan (ThreadSanitizer) | Unsynchronized concurrent memory accesses where at least one access is a write |
| UBSan (UndefinedBehaviorSanitizer) | Selected undefined behavior, such as signed integer overflow, invalid shifts and misaligned access |

ASan/UBSan and TSan use separate builds. They add execution overhead and are
disabled for published performance measurements. A passing run covers the paths
and thread schedules exercised; it does not prove that all errors are absent.

## Performance Matrix

The original IO-topology baseline and the expanded workload matrix are separate
datasets. [Payload, batch and execution comparisons](performance/dimensions.md)
hold connection counts constant while varying the other dimensions.

### Payload, Batch and Execution Modes

Maintainers can collect complete matrices with `bash scripts/benchmark_matrix.sh`.
Only this batch tool requires `jq`, GNU `timeout` (`gtimeout` on macOS) and
`shasum`, in addition to the usual shell tools. It launches the same C++ programs
and preserves their commands, JSON, failures, host details and hashes.

```sh
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_loopback_benchmark \
  --backend standalone --certs tests/certs --clients 16 --windows 1 4 16 64 \
  --payloads 64 1024 16384 --io-models shared sharded --io-threads 1 4 \
  --seconds 1 --warmup 0.25 --repetitions 3 --keep-going \
  --output benchmarks/results/local-dimensions-standalone-callback.json
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_coroutine_benchmark \
  --backend standalone --certs tests/certs --protocols tcp --clients 16 \
  --windows 1 4 16 64 --payloads 64 1024 16384 \
  --io-models shared sharded --io-threads 1 4 --work-values 0 10000 \
  --execution-modes callback coroutine --seconds 1 --warmup 0.25 --repetitions 3 \
  --output benchmarks/results/local-dimensions-standalone-native.json
```

Repeat both commands with the executables from the Boost build directory, set
`--backend boost`, and write `local-dimensions-boost-callback.json` and
`local-dimensions-boost-native.json`. These matrices contain **756 arknet
callback measurements and 432 matched native TCP measurements per backend**.
All seven protocols use the four explicit outstanding-request limits, including
UDP. Batch size and connection count are independent settings.

The native program compares Asio callbacks and C++20 coroutines with identical
full-batch server reads/echoes, full-batch client writes/reads, payload checks, CPU
computation and per-connection strands. It measures batch completion RTT;
one request per batch is equivalent to message RTT. Public arknet coroutine
endpoint APIs remain TODO. Its CPU time includes warmup and measurement for
both execution modes. Each server batch applies the configured computation to
every payload-sized message before echoing the batch.

`--keep-going` retains unsuccessful load cases, finishes the requested matrix
and returns nonzero if any case failed. Failed groups remain visible with their
errors and UDP losses; they are excluded from valid throughput/RTT comparisons.
Each configuration runs three times in a fresh process. Native execution order
alternates between repetitions. This closed-loop load reports response RTT,
including queueing; it does not measure an independently scheduled arrival rate.
Optionally create the offline report environment under
[Metrics and Charts](#metrics-and-charts), then generate the comparison from all
four data files after measurement has finished:

```sh
build/report-venv/bin/python benchmarks/dimensions.py \
  --reports benchmarks/results/local-dimensions-standalone-callback.json \
            benchmarks/results/local-dimensions-boost-callback.json \
            benchmarks/results/local-dimensions-standalone-native.json \
            benchmarks/results/local-dimensions-boost-native.json \
  --output-dir docs/performance
```

### Original IO-Topology Baseline

```sh
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_loopback_benchmark \
  --certs tests/certs --seconds 1 --warmup 0.25 --repetitions 3 \
  --output benchmarks/results/local-loopback.json
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_coroutine_benchmark \
  --certs tests/certs --protocols tcp --work-values 0 10000 \
  --seconds 1 --warmup 0.25 --repetitions 3 \
  --output benchmarks/results/local-coroutine.json
```

Per backend, the callback matrix has **630 measurements**:
seven protocols × three payloads × two traffic profiles × five topologies ×
three repetitions. The coroutine TCP matrix has **180 measurements**, adding
two handler-work levels. Default runner durations are three measured seconds
and one warmup second; the commands above match the shorter local baseline.
No-TLS runs should specify `--protocols tcp udp websocket http`.
These commands reproduce the baseline load settings on the current checkout.
The published baseline records its original source and binary hashes; use that
archived source to reproduce the earlier implementation.

| Dimension | Values |
| --- | --- |
| Protocol | TCP with `use_dgram`, UDP, WebSocket, TCPS, WSS, HTTP, HTTPS |
| Payload | 64, 1024, 16384 bytes |
| Traffic | 1 client / window 1; 16 clients / window 16 (UDP window 1) |
| IO | One context run by 1, 2 or 4 threads; 2 or 4 contexts run by one thread each |
| Coroutine work | 0 or 10000 deterministic CPU iterations per server echo |

One `io_context` can run on one or several threads. Multithreaded configurations
use independent per-connection strands; partitioned configurations run each
context on one thread. Both endpoints share the IO scheduling resources.
With one context the two configurations are identical, so the matrix measures
that case once. [IO models and coroutine guidance](threading.md) explains the
business tradeoffs.

Every measurement starts in a fresh process, warms up, measures, then drains
pending traffic for at most two seconds. Stop concurrent builds and other
heavy processes before collecting data. The runner saves partial failures;
content/order errors, rejected/completion-error sends, lost UDP messages,
unexpected disconnects and unfinished traffic invalidate a measurement.

## Metrics and Charts

| Metric | Definition |
| --- | --- |
| Round trips/s | Completed echoes divided by measured wall time |
| Payload MiB/s | `2 × payload bytes × completed echoes / seconds / 1048576`; headers and encryption excluded |
| RTT p50/p95/p99 | From submission to matching echo, including queueing and handler work; sampled |
| Average CPU utilization | Process CPU time divided by wall time over the same interval, multiplied by 100%; one core is 100%, multithreaded usage can exceed 100% |
| Peak RSS | Whole-process peak resident bytes; not a leak proof |
| Errors | Content/order, send completion/admission, lost/unfinished traffic, exceptions and drain timeouts |

CPU includes all client and server threads in the same process, not power consumption.
On this 14-logical-core host, process usage of 400% is about 28.6% of total CPU
capacity. New executables record `cpu_elapsed_seconds`. Historical results did not
record a separate wall-clock CPU sampling interval, so reports label utilization
as estimated: measurement plus drain for protocol tests, warmup plus measurement
for the matched native callback/coroutine comparison.

Both benchmark executables and the batch script accept `--address-family ipv6`
to test `::1`; the default `ipv4` uses `127.0.0.1`. Published performance reports
measure IPv4. See [IPv4 and IPv6](addressing.md) for usage and validation scope.

UDP requests 64 KiB send and 4 MiB receive buffers. Raw JSON records actual
kernel sizes for the server and client min/max; the OS can clamp requests.
The default UDP window does not model an unreliable network.
The measured host's loopback interface `lo0` has an MTU of 16384 bytes.

Python and Matplotlib are optional offline report tools. They are not used to
compile, launch or measure the C++ network tests. After measurement, generate
charts and bilingual tables from complete runs on the same host, source hash
and measurement duration:

```sh
python3 -m venv build/report-venv
build/report-venv/bin/pip install -r benchmarks/requirements-report.txt
build/report-venv/bin/python benchmarks/report.py \
  --reports benchmarks/results/local-loopback.json benchmarks/results/local-coroutine.json \
  --output-dir docs/performance \
  --host-description "Actual hardware, compiler, dependencies and Release build flags"
```

Windows uses `build/report-venv/Scripts/python.exe`. Matplotlib is only a
report-generation dependency. The generator refuses incomplete, failed,
duplicate or incompatible measurements. Charts show medians and min/max
across repetitions; these whiskers are not confidence intervals. Reported
latency is a median of per-run percentiles, not a pooled percentile.

Report-tool maintainers can run the optional Python checks separately:

```sh
build/report-venv/bin/python -m unittest discover -s benchmarks -p 'test_report.py' -v
build/report-venv/bin/python -m unittest discover -s benchmarks -p 'test_dimensions.py' -v
```

With several requests in flight per connection, the coroutine benchmark measures
a **batch RTT**, whereas callback traffic
replenishes a window and measures **message RTT**. Compare topology within
a benchmark program; do not infer a coroutine-versus-callback speedup from this
original baseline. The matched native comparison above uses one measurement
method for both execution modes.

## CI and Remaining Coverage

CI checks Linux/macOS/Windows with both providers; Linux also runs
ASan/UBSan and TSan. Job names include OS, provider, TLS and check mode.
Main pushes and PRs are separate triggers; outdated runs are canceled.
CI uploads test logs and runs benchmark smoke checks, not a performance
report matrix.

Local loopback cannot predict cross-host capacity or WAN latency. Loss and
delay injection, descriptor exhaustion and production-length endurance
require separate validation. Files under `benchmarks/results/macos-arm64-*`
are historical pre-rewrite evidence and do not establish current performance.

All 14 independent client/server usage programs were also built with both
backends. The seven protocol pairs per backend verified echo contents and all
28 processes exited normally. [Build/run logs and reproduction scripts](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/usage/README.md)
retain this check separately from timed performance measurements.
