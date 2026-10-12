# Testing and Validation

[Local performance report](../performance/overview.md)

[UDP retest and buffer diagnosis](../performance/udp-update.md) · [Buffer configuration](../guides/socket-buffers.md)

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
| Standalone Asio | Release | ON | 40 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone.log) |
| Standalone Asio | Release | OFF | 30 / 87 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-notls.log) |
| Standalone Asio | Debug | ON | 40 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-debug-tls.log) |
| Standalone Asio | Debug | OFF | 30 / 87 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-debug-notls.log) |
| Boost | Release | ON | 40 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost.log) |
| Boost | Release | OFF | 30 / 87 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-notls.log) |
| Boost | Debug | ON | 40 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-debug-tls.log) |
| Boost | Debug | OFF | 30 / 87 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-debug-notls.log) |
| Standalone Asio | Debug + ASan/UBSan | ON | 15 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-asan.log) |
| Standalone Asio | Debug + TSan | ON | 15 / 115 | [Passed](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-tsan.log) |

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

After correcting the UDP client's implicit `SO_REUSEADDR` setting on 2026-10-12,
both Release + TLS backends passed **41 checks / 134 cases**, and each passed
30 independent executions of the previously failing UDP configuration.
UDP ASan/UBSan and TSan checks also passed. See the
[UDP endpoint regression record](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261012/udp-endpoints/README.md)
for the Linux port-collision evidence and local logs.

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
