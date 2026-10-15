# Testing and Validation

[Local performance report](../performance/overview.md)

[UDP report and buffer diagnosis](../performance/udp-update.md) · [Buffer configuration](../guides/socket-buffers.md)

Functional tests establish correctness; sustained stress checks repeated work,
restarts and cleanup. Performance measurements run locally after those checks
pass and use a Release build without sanitizers. CI checks regressions and
smoke execution; its shared-runner timing is not the published baseline.

## Build and Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_TESTS=ON \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_USE_BOOST_ASIO=ON
cmake --build build --parallel 3
cmake --build build --target arknet_check
build/tests/arknet_stress_test --protocol all --seconds 60 \
  --clients 16 --rounds 2 --window 4 --certs tests/certs
```

For Standalone Asio, use a separate build directory with
`-DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`.
HTTP and WebSocket checks require Boost.Asio and Boost.Beast; the two
configurations therefore contain different checks. Windows executables have the `.exe` suffix.
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
| HTTP requests | Independent raw peers, HEAD/interim/chunked/EOF responses, cancellation, deadlines, limits and exactly-once completion |
| ARQ / RUDP | Canonical protocol interop, loss/reordering, bounded memory, handshake retries, IPv4/IPv6, stale SYN handling and shutdown |
| FEC / RUDP FEC | Independent Reed–Solomon interop, negotiation, CRC rejection, duplicate/replay handling, recovery, partial groups, memory budgets, fixed/adaptive policies and concurrent statistics |
| Lifecycle / detail / IO pool | Timer/post cancellation, real completion before destruction, mutable listeners, executor serialization, queued work and restart |
| Process exit / thread storage | Last-owner callback release, nested pools, direct/indirect globals, both multi-TU link orders, late-construction rejection and explicit Asio configuration preservation |
| Concurrency / keepalive | Send limits, callback completion accounting, concurrent shutdown, TCP keepalive options |
| TLS | Trust/identity rejection, mTLS, deadlines, WSS and fresh stream on reconnect |
| IPv6 | TCP, UDP/session/cast, WS/WSS, HTTP/HTTPS, TLS IP identity and handshake Host |
| MySQL | Packet/authentication/command codecs, verified TLS, queries, prepared statements, transactions, result limits, cancellation, bounded admission and exclusive pool reset/database isolation |
| Redis | RESP2/RESP3, authentication, TCP/TLS/mTLS, ordered pipelines, cancellation reply draining, bounded admission, unsupported replication commands and exclusive pool reset/authentication/database isolation |
| Installed consumer | Provider/TLS configuration propagation and installed public headers |

Sustained stress validates content/sequence, completion counts, connection and
server restarts, pending writes and queue cleanup. Each protocol splits
`--seconds` across the built-in scheduler, one context/one thread, one context/four
threads and four contexts/one thread each,
then across the requested restart rounds. Seven protocols with
`--seconds 60` therefore request seven minutes of traffic in total, plus setup
and teardown. Slow receivers test backpressure rather than throughput.

## Local Verification

The complete-suite source snapshot is `6c8418e`; its networking changes are
included in measured revision `2aa5bc0`. The separate test-only snapshot
`25bb9f8` covers allocation-failure portability and native-connect ordering below.
[PR #16](https://github.com/OpenArkStudio/arknet/pull/16) records the candidate
review and cross-platform CI; local results do not substitute for its checks.

Validation date: 2026-10-15. The machine is a Mac mini `Mac16,11`, Apple M4 Pro, 14 physical/logical cores
(10 performance and 4 efficiency cores), 48 GiB RAM, macOS 27.0.1 (26A434).
Dependencies are Standalone Asio 1.38.2, Boost 1.90.0 and OpenSSL 3.6.4.
Ordinary builds use Apple Clang 21.0.0; sanitizer builds use LLVM 22.1.7.

### Complete Checks

All four builds use Debug, TLS and tests ON, examples and benchmarks OFF.
Compilation finishes before checks run serially with real database services.

| Dependency / instrumentation | Checks | doctest cases | Assertions | Result |
| --- | ---: | ---: | ---: | --- |
| Boost.Asio | 52/52 | 470 | 978,433 | Passed |
| Standalone Asio | 51/51 | 410 | 576,824 | Passed |
| Boost.Asio / ASan+UBSan | 52/52 | 470 | 615,937 | Passed |
| Standalone Asio / TSan | 51/51 | 410 | 333,108 | Passed |

Checks count programs invoked by `arknet_check`; cases count doctest executions.
Assertion totals are observed counts, including duration-based stress cases.
The enabled default suites have zero failures or environment skips. Archived
Zephyr interoperability is not enabled in these runs; its independent evidence
is recorded below. Reference ARQ/FEC interoperability is included.

Each configuration includes external-context stop (15 cases / 375 assertions),
queued-connect cancellation (4 / 44), protocol and runtime-exit suites, stress,
and installed-package consumption. The installed-consumer child build does not
inherit sanitizer flags; its binaries are not sanitizer-instrumented.

### Database and Shutdown Coverage

All four configurations execute the optional real-service cases against isolated
MySQL Community 8.4.11 and Redis 7.4.2, without database-case skips. MySQL verifies
TLS; Redis covers TCP, TLS and mTLS. Coverage includes connection/client and pool
operations, permanent `close_and_wait()`, late connects, callback/capture retirement,
publication failures and recovery, cancellation, and reset isolation.
Without the documented service environment variables, real-service cases skip
rather than substitute simulated services. Performance uses Valkey 8.0.11 for
the Redis protocol; it is separate from this Redis 7.4.2 interoperability evidence.

TCP-based endpoint checks cover native completion and capture retirement.
Complete concurrent owner-stop serialization is verified for TCP servers and
HTTP/WebSocket/TLS-derived servers. It is not a new guarantee for every protocol.
External-context owners keep their runners alive through drain and stop external
producers first; worker callbacks must not block waiting for themselves.
See [lifecycle](../concepts/lifecycle.md) and the formal
[shutdown and fault tests](https://github.com/OpenArkStudio/arknet/pull/16/files).
These results do not establish dynamic-library unloading safety.

### Additional Focused Checks

| Scope | Checks | Cases | Assertions | Result |
| --- | ---: | ---: | ---: | --- |
| Boost.Asio, TLS OFF: eight selected targets | 8/8 | 66 | 1,515 | Passed |
| Boost.Asio ASan+UBSan: queued-connect, HTTP external-context and external-stop repetitions | 72/72 | 280 | 4,664 | Passed |

The NoTLS selection covers TCP, external-stop, manual-runtime, queued-connect
cancellation and four Redis shutdown/publication-failure targets; it is not a
complete NoTLS matrix or a MySQL-without-OpenSSL result. The 72 ASan executions
comprise 32 queued-connect runs, 32 selected HTTP cases and eight external-stop
runs. Cases excluded by the explicit HTTP filter are not environment skips.
The complete unfiltered HTTP suite passes in the complete checks above.

The test-only allocation and connect-ordering snapshot has separate coverage:

| Dependency / instrumentation | Focused cases | Assertions | Selected native-error executions |
| --- | ---: | ---: | ---: |
| Boost.Asio / ASan+UBSan | 30 | 927 | 32/32 passed |
| Standalone Asio / ASan+UBSan | 30 | 927 | 32/32 passed |
| Boost.Asio / TSan | 30 | 926 | 32/32 passed |
| Standalone Asio / TSan | 30 | 926 | 32/32 passed |

All focused cases pass without sanitizer diagnostics. The 128 selected
native-error executions each check nine assertions; other cases excluded by the
filter are not environment skips. These runs are additional to the complete
snapshot, not a complete-suite run of the test-only changes. The MSVC-specific
CRT allocation path requires Windows CI and remains pending; macOS cannot
execute that path.

ASan leak checking is disabled on this macOS host. The complete-suite ASan runs
also disable online symbolization to avoid a diagnosed symbolizer hang; memory
instrumentation remains enabled. No sanitizer suppressions or forced-include
workarounds were added. Only the existing reference-KCP test translation unit
excludes `null,object-size` instrumentation.

### RUDP Interoperability Evidence

The following targeted runs were recorded on 2026-10-13. Their source revisions
and scope remain in the linked record; their earlier complete-check counts do
not describe the current project.

| Instrumentation | Scope | Cases | Result |
| --- | --- | ---: | --- |
| ASan/UBSan | Standalone Asio: ARQ, reference ARQ/FEC interop, RUDP/FEC, lifecycle, detail and concurrency | 109 | Passed |
| TSan | Standalone Asio: FEC, reference FEC interop, RUDP/FEC, lifecycle, detail and concurrency | 86 | Passed |

The RUDP FEC endpoint suite passed 11 cases and 2,564 assertions in both Release
dependency configurations and the separate ASan/UBSan and TSan builds. Real zephyr/arknet process
pairs passed all 12 scenarios per dependency configuration, including reconnect, ARK-style binary
headers, 4096-byte bodies, ARQ packet loss and reordering. The relay preserves
SYN/ACK/FIN; this does not establish reliable shutdown when FIN is lost.

[Logs and reproduction commands](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261013/rudp-fec/README.md)
record the source revision and exact scope. The test-only canonical KCP source
disables `null,object-size` instrumentation; arknet's own code remains fully instrumented.
RUDP performance has a [separate report](../performance/rudp.md).

<Aside type="note" title="Deployment capacity">

Local correctness, sanitizer and loopback results do not establish a
100,000–200,000 cluster PCU capacity. Cross-host load, resource budgets,
production-duration endurance and ARK workload acceptance require separate tests.

</Aside>

Sanitizers run separately from performance measurements. LeakSanitizer support
differs by platform; a passing Apple ASan run is not a Linux leak-check result.

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

CI checks Linux/macOS/Windows with both dependency configurations; Linux also runs
ASan/UBSan and TSan. Job names include OS, provider, TLS and check mode.
Main pushes and PRs are separate triggers; outdated runs are canceled.
CI uploads test logs and runs benchmark smoke checks, not a performance
report matrix.

Local RUDP measurements cover random/burst loss, delay, duplication and
reordering, with six 120-second sustained runs. They cannot predict cross-host
capacity or WAN latency. Descriptor exhaustion, production-duration endurance
and [ARK business acceptance](production-readiness.md) remain to be validated.

All 14 independent client/server usage programs were also built with both
dependency configurations. The seven protocol pairs per dependency configuration verified echo contents and all
28 processes exited normally. [Build/run logs and reproduction scripts](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/usage/README.md)
retain this check separately from timed performance measurements.
