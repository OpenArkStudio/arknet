# Testing and Validation

[Local performance report](../performance/overview.md)

[UDP report and buffer diagnosis](../performance/udp-update.md) · [Buffer configuration](../guides/socket-buffers.md)

Functional tests establish correctness; sustained stress checks repeated work,
restarts and cleanup. Performance measurements run locally after those checks
pass and use a Release build without sanitizers. CI checks regressions and
smoke execution; its shared-runner timing is not the published baseline.

## CI coverage

Source PRs run formatting, documentation and related native modules in a Linux
Boost/TLS Release job. RUDP/ARQ/FEC, UDP, HTTP/WebSocket, MySQL and Redis paths
have explicit module mappings; combined changes run their union. Shared TCP,
common headers, build/dependency/CI changes and unclassified paths run the entire
Linux Boost/TLS Release suite, examples and benchmark smoke checks.

Code-bearing merge-queue candidates and manual runs retain the complete native
matrix: Linux/macOS/Windows, Boost/Standalone Asio, TLS, Linux without TLS, and
Linux AddressSanitizer with UndefinedBehaviorSanitizer, and separate ThreadSanitizer
checks. A green source PR is not full-matrix acceptance.

Strictly classified README/documentation-only changes run documentation and
formatting checks in both stages. The twelve matrix jobs retain their required
names; cells outside the planned source coverage explicitly record scope-only
validation, not executed native tests. Missing or malformed change ranges select
the complete matrix. Failed scope validation blocks every native gate. Rename
classification includes both old and new paths. Active runs are not cancelled to
schedule a newer commit.

`CI - required` accompanies twelve static native matrix checks, documentation
and formatting: fifteen required check contexts in total. Scope-only runner jobs
retain their existing contexts and native commands. The gate uses the built-in
token with job-local `contents: read` and
`actions: read` to read this run's actual job and step metadata. It recomputes the
scope from the event's exact commit range and requires matching scope outputs,
successful documentation and formatting, and exactly one successful job for each
reviewed matrix profile. Missing, duplicate, incomplete or unknown results fail.

For full coverage, every cell must actually complete configuration, build and its
matching test steps. A source-module plan runs native checks only in Linux Boost/TLS
Release; other cells must successfully record scope-only coverage and skip native
steps. Documentation-only plans require all twelve cells to record scope-only
coverage. A successful job conclusion cannot substitute for a skipped required
step, a different test profile or a source result on a merge candidate. The gate
also checks the benchmark/report steps wherever the existing plan requires them.

The gate tolerates `failure` only for the explicitly optional cache installation,
archive restore and statistics steps, plus the corresponding action post steps,
when that cell runs native checks. The job itself and all required preparation,
configuration, build and test steps must still succeed. Disabled restores may be
skipped; a failed compiler-cache installation must skip its restore and startup
and use the direct compiler. Scope-only cells must skip every cache step and any
recorded cache post step. Missing steps, unknown outcomes and failures from other
steps remain errors. Fixture coverage establishes these fallback rules, not cache
hits, hosted acceptance or a measured speedup.

The matrix retains its static job contexts even for scope-only plans. Local
fixture tests establish the gate's validation rules; they do not establish hosted
native acceptance or a reduction in runner use.

For a focused local run, pass exact registered check names. Omit the option to
run all checks; unknown or unavailable names fail during configuration:

```sh
cmake -S . -B build -DARKNET_ENABLE_SSL=ON '-DARKNET_CHECK_TESTS=arq_core;fec'
cmake --build build --target arknet_check --parallel 2
```

Download the [validation logs and source mapping](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-validation-logs.tar.gz) with the [SHA256 checksum](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-validation-logs.tar.gz.sha256). The archive includes bilingual instructions and an independent verifier.

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
| Installed consumer | Provider/TLS configuration, installed public headers and optional C++ compiler launcher forwarding; empty launcher, paths with spaces, launcher argument lists and real compiler failure propagation |
| Connection-load process | Raw TCP peers split the frame prefix/body and combine eight requests across a 4 KiB receive capacity against both arknet and native Asio servers; ordered byte-exact replies and FIN exchange, not a high-concurrency or TCP-loss test |

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
Pool-shutdown coverage identifies its separate source snapshot `550fb58`.
[PR #16](https://github.com/OpenArkStudio/arknet/pull/16) records the candidate
review and cross-platform CI; local results do not substitute for its checks.

Validation dates: 2026-10-15–16. The machine is a Mac mini `Mac16,11`, Apple M4 Pro, 14 physical/logical cores
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
| Boost.Asio / AddressSanitizer+UndefinedBehaviorSanitizer | 52/52 | 470 | 615,937 | Passed |
| Standalone Asio / ThreadSanitizer | 51/51 | 410 | 333,108 | Passed |

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
| Boost.Asio AddressSanitizer+UndefinedBehaviorSanitizer: queued-connect, HTTP external-context and external-stop repetitions | 72/72 | 280 | 4,664 | Passed |

The NoTLS selection covers TCP, external-stop, manual-runtime, queued-connect
cancellation and four Redis shutdown/publication-failure targets; it is not a
complete NoTLS matrix or a MySQL-without-OpenSSL result. The 72 AddressSanitizer executions
comprise 32 queued-connect runs, 32 selected HTTP cases and eight external-stop
runs. Cases excluded by the explicit HTTP filter are not environment skips.
The complete unfiltered HTTP suite passes in the complete checks above.

The test-only allocation and connect-ordering snapshot has separate coverage:

| Dependency / instrumentation | Focused cases | Assertions | Selected native-error executions |
| --- | ---: | ---: | ---: |
| Boost.Asio / AddressSanitizer+UndefinedBehaviorSanitizer | 30 | 927 | 32/32 passed |
| Standalone Asio / AddressSanitizer+UndefinedBehaviorSanitizer | 30 | 927 | 32/32 passed |
| Boost.Asio / ThreadSanitizer | 30 | 926 | 32/32 passed |
| Standalone Asio / ThreadSanitizer | 30 | 926 | 32/32 passed |

All focused cases pass without sanitizer diagnostics. The 128 selected
native-error executions each check nine assertions; other cases excluded by the
filter are not environment skips. These runs are additional to the complete
snapshot, not a complete-suite run of the test-only changes. The MSVC-specific
CRT allocation path is covered by the Windows CI jobs; this macOS validation
does not execute it.

### Pool Shutdown Checks

Snapshot `550fb58` retires queued acquire requests in place in both pools,
avoiding allocation-dependent unordered-map move construction. Its formal
regressions cover two queued waiters, reentrant close/cancel/acquire, once-only
completion and capture retirement. Owner-side guards keep test state alive
through permanent closure on fatal assertion paths.

| Dependency / instrumentation | MySQL shutdown cases / assertions | Redis shutdown cases / assertions |
| --- | ---: | ---: |
| Boost.Asio / AddressSanitizer+UndefinedBehaviorSanitizer | 14 / 1,067 | 19 / 596 |
| Standalone Asio / AddressSanitizer+UndefinedBehaviorSanitizer | 14 / 1,067 | 19 / 596 |
| Boost.Asio / ThreadSanitizer | 14 / 1,067 | 19 / 595 |
| Standalone Asio / ThreadSanitizer | 14 / 1,066 | 19 / 595 |

All eight target runs pass with zero failures, skips or sanitizer reports.
The four configurations also pass unchanged MySQL return-failure tests
(5 cases / 222 assertions) and Redis return-failure tests (4 / 107).
Those return-failure runs preceded the guard-only test edit; their tested
source files and both pool headers match this snapshot. These are focused
checks, not a complete-suite run of this snapshot.

Redis diagnostic tests retain inspected exception snapshots until their
concurrent observer joins. The original Linux ThreadSanitizer and Windows fault-injection
failures, source mapping and focused logs remain in the validation archive.
The pool correction removes one allocation boundary; it does not guarantee
that every child-close path can recover from heap exhaustion. Performance
measurements remain tied to `2aa5bc0`, before this shutdown-only correction.

AddressSanitizer leak checking is disabled on this macOS host. The complete-suite AddressSanitizer runs
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
| AddressSanitizer/UndefinedBehaviorSanitizer | Standalone Asio: ARQ, reference ARQ/FEC interop, RUDP/FEC, lifecycle, detail and concurrency | 109 | Passed |
| ThreadSanitizer | Standalone Asio: FEC, reference FEC interop, RUDP/FEC, lifecycle, detail and concurrency | 86 | Passed |

The RUDP FEC endpoint suite passed 11 cases and 2,564 assertions in both Release
dependency configurations and the separate AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer builds. Real zephyr/arknet process
pairs passed all 12 scenarios per dependency configuration, including reconnect, ARK-style binary
headers, 4096-byte bodies, ARQ packet loss and reordering. The relay preserves
SYN/ACK/FIN; this does not establish reliable shutdown when FIN is lost.

[Logs and reproduction commands](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec/README.md)
record the source revision and exact scope. The test-only canonical KCP source
disables `null,object-size` instrumentation; arknet's own code remains fully instrumented.
RUDP performance has a [separate report](../performance/rudp.md).

<Aside type="note" title="Deployment capacity">

Local correctness, sanitizer and loopback results do not establish a
100,000–200,000 cluster PCU capacity. Cross-host load, resource budgets,
production-duration endurance and ARK workload acceptance require separate tests.

</Aside>

Sanitizers run separately from performance measurements. LeakSanitizer support
differs by platform; a passing Apple AddressSanitizer run is not a Linux leak-check result.

### Sanitizer Checks

Sanitizers instrument a development build and report errors observed while tests run.

| Tool | Checks |
| --- | --- |
| AddressSanitizer | Memory errors such as out-of-bounds access, use-after-free and double-free |
| ThreadSanitizer | Unsynchronized concurrent memory accesses where at least one access is a write |
| UndefinedBehaviorSanitizer | Selected undefined behavior, such as signed integer overflow, invalid shifts and misaligned access |

AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer use separate builds. They add execution overhead and are
disabled for published performance measurements. A passing run covers the paths
and thread schedules exercised; it does not prove that all errors are absent.

## CI and Remaining Coverage

CI checks Linux/macOS/Windows with both dependency configurations; Linux also runs
AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer. Job names include
OS, dependency, TLS and check mode. The twelve native checks, documentation and
formatting accompany `CI - required`. Known source
modules on PR branches run their selected native checks in Linux/Boost/TLS/Release;
other cells record scope-only coverage. Code-bearing merge-queue candidates,
manual dispatches and changes outside the reviewed module map run all twelve native
cells. Documentation-only candidates run documentation and formatting. A successful
PR head does not validate the queue candidate containing
the latest main and preceding queued changes. CI does not repeat the same matrix
after the candidate is merged into main. The README badge shows merge-queue runs.

Runs keep the existing per-PR or ref concurrency group. A new run does not cancel
the active run in that group; it waits for the group. GitHub may replace an older
pending run in the same group, so this setting is not a FIFO history of every push
([GitHub concurrency rules](https://docs.github.com/en/actions/how-tos/write-workflows/choose-when-workflows-run/control-workflow-concurrency)).
Required checks on the candidate still need to succeed. Documentation publishing
keeps its separate main-push workflow. CI uploads test logs and runs benchmark
smoke checks; performance report matrices come from local measurements.

Local RUDP measurements cover random/burst loss, delay, duplication and
reordering, with six 120-second sustained runs. They cannot predict cross-host
capacity or WAN latency. Descriptor exhaustion, production-duration endurance
and [ARK business acceptance](production-readiness.md) remain to be validated.

All 14 independent client/server usage programs were also built with both
dependency configurations. The seven protocol pairs per dependency configuration verified echo contents and all
28 processes exited normally. [Build/run logs and reproduction scripts](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261010/usage/README.md)
retain this check separately from timed performance measurements.

### Bounded compiler parallelism

The accepted [merge-candidate run 39372855018](https://github.com/OpenArkStudio/arknet/actions/runs/39372855018),
for candidate `36e4e184b6d145c2fd03dbf1e99107b590b518ad`, records these elapsed
minutes. They describe one hosted run, not a controlled performance comparison:

| Profile | Job | Configure | Build | Test |
| --- | ---: | ---: | ---: | ---: |
| Linux Boost TLS, AddressSanitizer + UndefinedBehaviorSanitizer | 58.35 | 3.42 | 50.68 | 3.45 |
| Linux Standalone TLS, AddressSanitizer + UndefinedBehaviorSanitizer | 33.02 | 2.92 | 27.28 | 2.27 |
| Windows Boost TLS Release | 56.47 | 10.88 | 37.77 | 6.35 |

Only the Linux Boost build with AddressSanitizer and
UndefinedBehaviorSanitizer can experimentally select two compiler
slots. Immediately before building, a small Node helper requires all of:

- `os.availableParallelism()` reports at least four available CPU slots.
- The effective memory ceiling is at least 15 GiB: use the lesser of actual
  `os.totalmem()` and a positive `process.constrainedMemory()` limit.
- `process.availableMemory()` independently reports at least 12 GiB still available
  to the process, without exceeding that ceiling.

A zero constrained-memory result is recorded as `no-reported-constraint` and does
not supply another numeric ceiling; the helper then uses actual total memory and
still requires the independent available-memory and CPU checks. Zero does not
prove unlimited memory. A positive constraint must be a finite integer and is
first capped at the already validated physical total; the effective ceiling must
then be a safe integer. An external limit above that total is reported as
`reported-above-host-memory`, without inferring unlimited memory. CPU, physical
total and available-memory observations must remain positive safe integers;
nonfinite, negative, fractional or string constraints are invalid. Missing,
invalid, inconsistent or insufficient resource observations select one slot. Other profiles keep their existing parallelism,
and source-module targets, sccache launchers, all twelve matrix profiles, native
tests and the 60-minute job limit remain unchanged. The required gate verifies
that the planning step ran successfully only in its intended native cell.

The helper prints the actual observations, thresholds, selected bound and reason
to the job log and summary. This is a conservative scheduling heuristic: 12 GiB
of available memory is an admission allowance for both slots and their overhead,
not a measured compiler peak or an enforced per-slot limit. The timing metadata
contains no compiler/linker peak memory, and application benchmark RSS cannot
supply it. A successful complete hosted matrix is required before merging this
change; speedup is not established locally. An out-of-memory failure must retain
its logs for correction, without an automatic retry that hides the failure.

API semantics: [Node OS resource APIs](https://nodejs.org/api/os.html#osavailableparallelism),
[constrained memory](https://nodejs.org/api/process.html#processconstrainedmemory),
and [available memory](https://nodejs.org/api/process.html#processavailablememory).

### Native build caches

Every active native matrix cell uses the official Mozilla sccache action `v0.0.11`
with sccache `v0.16.0`. CMake passes the cache through both compiler launcher options;
the compiler, build type, flags, selected targets and test commands stay the same.
Compiler setup or server startup failure selects direct compilation. Server IO failure
uses `SCCACHE_IGNORE_SERVER_IO_ERROR=1` to fall back to the local compiler.

The installed-consumer child configure also receives the parent build's optional
`CMAKE_CXX_COMPILER_LAUNCHER`, including spaces in its executable path and
semicolon-separated launcher arguments. An empty value adds no cache-tool
dependency; compiler errors still fail the build. The `consumer_launcher`
regression uses real child builds for these boundaries; the complete
`install_consumer` check remains independent. Forwarding a launcher does not
establish a cache hit or compilation-time saving.

GitHub Actions persists the local compiler cache and vcpkg `files` binary archives
separately. Each compiler cache is bounded to 512 MiB. Cache keys cover OS,
architecture, runner image/version, target and host triplets, pinned vcpkg revision, manifest/configuration/overlay contents, requested
features, default features and the matrix build profile. Release,
AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer profiles remain
separate. vcpkg also validates each package ABI before reuse. Dependency archives
require an exact key; compiler caches use a profile-specific prefix plus a unique
run suffix so later runs can add objects. No build directory or test result is cached.

Actions cache visibility also depends on the workflow ref. Runs can restore matching
caches from their current ref and the default branch. A PR merge-ref cache is limited
to that PR; another PR or `main` cannot restore it. Merge-queue refs are temporary, so
their writes also do not populate `main`. Routine PR and queue runs therefore do not
maintain a shared default-branch cache.

Seed shared caches explicitly with the existing **CI**
workflow: select **Run workflow**, choose branch **main**, then run it. This manual
`workflow_dispatch` executes the full native matrix and saves successful cache
entries under `main`. Repeat after runner image, dependency or build-profile changes,
or when refreshing compiled objects after source changes. Until the next explicit
seed, new PRs and queue candidates may recompile objects absent from the main cache.
No push-triggered or scheduled matrix is added, and PR/queue artifacts are not
promoted into main caches. See [GitHub cache scope restrictions](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache).

Missing cache identity or unavailable archive storage disables persistence. Cache
restore/save failures do not replace compilation or testing. The job summary records
archive restore status, dependency archive count and `sccache --show-stats`; Configure
logs provide vcpkg package restore/build counts. An Actions archive hit alone does not
prove compiler or package hits. Compare cold and warm runs of the same revision and
profile on hosted runners before claiming a speedup; local contract checks do not
measure hosted performance. Existing source-PR scope and all twelve full-matrix cells
are unchanged.

References: [Mozilla action](https://github.com/Mozilla-Actions/sccache-action/tree/v0.0.11),
[sccache launchers and fallback](https://github.com/mozilla/sccache/tree/v0.16.0),
[vcpkg binary cache configuration](https://learn.microsoft.com/en-us/vcpkg/reference/binarycaching).
