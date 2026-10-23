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
formatting checks in both stages. Seven grouped native jobs retain all twelve
configuration profiles; cells outside the planned source coverage explicitly
record scope-only validation. Missing or malformed change ranges select
the complete matrix. Failed scope validation blocks every native gate. Rename
classification includes both old and new paths. Active runs are not cancelled to
schedule a newer commit.

Documentation paths include `AGENTS.md`, Markdown and documentation assets, plus
the explicitly mapped Nimbus site configuration, dependencies, Astro/TypeScript
components, pages and content tooling. This is not a blanket `docs/site/**`
exemption: CI scope/workflow tests, native release/report checks, C++/CMake files,
unknown scripts and unmapped file types retain native coverage. A mixed change
uses the native plan at both stages.

The workflow defines seven native group contexts, documentation, formatting and
`CI - required`: ten contexts after the protection migration described below.
The existing fifteen-context protection is a separate deployment state; changing
this source does not migrate it. The gate uses the built-in token with job-local
`contents: read` and `actions: read` to read this run's actual job and step metadata.
It recomputes scope from the event's exact commit range and requires matching scope
outputs, successful documentation and formatting, exactly one successful job for
each group, and configuration-specific step conclusions for all twelve profiles.
Missing, duplicate, incomplete or unknown results fail.

The required job collects metadata through the GitHub CLI for the explicit
`GITHUB_RUN_ID` and `GITHUB_RUN_ATTEMPT`, using `gh run view --attempt` rather than
the default run snapshot. It verifies the returned run ID and attempt before
using any jobs. A successful job can temporarily expose pending or in-progress
step metadata. Only a structurally valid unfinished snapshot triggers a second
whole-snapshot read anchored to one of its concrete pending prerequisite jobs:
`gh run view <run-id> --repo <repository> --attempt <attempt> --job <job-id> --json databaseId,attempt,jobs`.
The anchor comes from that same snapshot, never the aggregate or another run.
Across retry cycles, selection rotates among that snapshot's pending prerequisites
in the verified name/ID set. Only selection history is retained; success states
from different anchored snapshots cannot be combined.
Both reads verify the run and attempt, all nine prerequisite job names/IDs must
stay identical across reads, and each new snapshot is independently inspected.
Completed jobs must already expose their required named steps; known required
failures or malformed/partial snapshots cannot trigger a refresh into success.
Explicitly optional native-cache failures retain their existing treatment.

If both plain and anchored reads remain pending, repeat with 1, 2, 4, 8 and then
15-second backoff. The 120-second total includes every CLI call; each call is
capped at 20 seconds or the remaining budget. The reported snapshot count counts
actual CLI reads, including anchored ones. The required job retains its existing
five-minute deadline.

Each structurally valid, identity-checked read emits one `Prerequisite snapshot:`
JSON diagnostic with its run/attempt, read number, `plain` or `anchored` mode,
anchor ID, elapsed milliseconds and SHA-256 `digest`. The digest covers only the
nine prerequisites' IDs, names, statuses, conclusions and numbered step states,
in stable job/step order. It excludes the running aggregate and unrelated payload
fields. Identical digests mean identical selected state, not proof of fresh data;
different digests identify a state change, not its cause or successful acceptance.

For a finalization timeout, compare those diagnostics to locate pending jobs and
steps before changing collection behavior. The `pending` list includes job names,
IDs, statuses and conclusions, plus at most four pending steps per job and sixteen
steps per read. Each job reports `pending_steps` and `omitted_steps`; the digest
still covers omitted steps. Names are limited to 96 ASCII characters, with control
and non-ASCII characters replaced; statuses/conclusions are limited to 24. Output
is one bounded JSON line, without raw payloads, environment, command output or URLs.
The diagnostics reuse already-read data and add no queries. They do not change the
120-second budget, provenance checks, native coverage or single-snapshot acceptance,
and do not establish a metadata-staleness root cause or a fix for it.

The running `CI - required` job is excluded from prerequisite readiness; its
identity must still be unique. The collector passes the actual finalized snapshot
to the same strict coverage validator before saving it, and the following gate
step validates it again. It never converts pending steps to success. CLI errors,
malformed or mismatched identities, missing/duplicate/unexpected jobs, unsuccessful
prerequisites and incomplete final coverage remain failures. A partial rerun that
omits prerequisites from the current attempt cannot borrow successful jobs from
an earlier attempt; use a fresh complete run for acceptance. The job anchor does not narrow validation to one job: the entire returned snapshot
still requires all nine prerequisites and twelve actual native profiles for a full
run. No snapshots are merged across reads or attempts, and no default-attempt
fallback is used.

A fresh complete run at the changed head must validate this collector. The failed
aggregate from an older manual run remains failed even if a later metadata read
shows successful native prerequisites; rerunning only that old gate does not
establish a new all-green acceptance run. Local fake-CLI and clock fixtures prove
collection and rejection rules, not hosted cache state or faster merging.

Full coverage requires each configuration to finish its own configure, build and
test steps. Source plans execute only Linux Boost/TLS Release; other profiles
record scope-only coverage and skip native steps. Documentation-only plans skip
all native steps. A successful job conclusion cannot replace a skipped required
step, a different sanitizer profile or a source result on a merge candidate.
Benchmark/report checks retain their existing Linux TLS Release coverage for both
dependency configurations when the plan selects the complete suite.

Only explicit optional cache installation, archive restore and statistics steps,
and the corresponding cache action post steps, may report `failure` in active
native groups. The group and required preparation/configure/build/test steps must
succeed. Failed compiler-cache installation disables its restore and startup and
selects direct compilation. Scope-only groups skip all cache steps and any recorded
cache post steps. Missing steps, unknown outcomes and other failures remain errors.
Local fixtures establish these rules, not hosted acceptance or a measured speedup.

### Grouped native jobs and protection migration

Release jobs share setup within bounded groups. Linux has separate Boost and
Standalone Asio jobs, each covering TLS and without TLS. Windows keeps each TLS
dependency configuration in its own job. macOS combines both TLS dependencies.
Linux AddressSanitizer with UndefinedBehaviorSanitizer and ThreadSanitizer remain
separate jobs, each executing Boost and Standalone Asio with TLS. These seven
jobs retain the same twelve profiles, without new platforms or TLS/sanitizer combinations.

Each configuration has a separate `build/ci-<boost>-<ssl>-<sanitize>` tree,
raw stage logs under its own `ci-logs/`, and named configure/build/test conclusions.
Stage logs stay on the runner and are not uploaded. Existing test-output attachments
retain their `tests/logs/**/*.log` coverage; Actions console output and step summaries
remain available for diagnosing stage failures.
Configuration steps explicitly run after earlier configurations fail; dependencies
within the failed configuration still skip invalid work. Any missing, failed or
skipped required configuration fails the group and aggregate gate. A group timeout
can prevent later configurations from running and therefore fails acceptance.

Only setup and cache containers are shared. All configurations continue requesting
the same `boost;ssl` dependency feature superset. Cache keys retain OS, architecture,
runner image, compiler environment, dependency identity and Release/sanitizer group,
including each group's Boost, Standalone or combined dependency selector.
`grouped-v2` separates the seven-group namespace from earlier cache layouts. Compiler
invocation hashes distinguish Boost/TLS flags inside the shared sccache container.
Build trees and results are never cached. Dependency ABI validation remains in
vcpkg. No concurrent native profiles share a runner: Release uses three build
slots, with resource-admitted four slots for Windows Boost as described below.
ThreadSanitizer stays at two, and AddressSanitizer/UndefinedBehaviorSanitizer at
one or the existing resource-admitted two slots, re-evaluated for each dependency.
Every group retains the 60-minute job deadline; it has not been extended to make
serial execution pass.

Windows Ninja builds serialize executable linking and its post-build DLL
deployment in one link pool. C++ compilation keeps the existing parallelism;
vcpkg deployment and all tests remain enabled. This prevents those build edges
from copying shared runtime files concurrently. External file locks can still
fail a build, and a failed command is reported without automatic retries.

The following names map the twelve existing native required contexts to seven
new contexts. Rows list the exact old names, including pairs of TLS modes or dependencies.
Documentation checks, `Style - clang-format 18` and `CI - required` retain their names.

| Existing native contexts (`Test - ` prefix) | New context |
| --- | --- |
| `ubuntu-latest - Boost - TLS - Release`; `ubuntu-latest - Boost - NoTLS - Release` | `Native - ubuntu-latest - Boost - Release` |
| `ubuntu-latest - Standalone - TLS - Release`; `ubuntu-latest - Standalone - NoTLS - Release` | `Native - ubuntu-latest - Standalone - Release` |
| `macos-latest - Boost - TLS - Release`; `macos-latest - Standalone - TLS - Release` | `Native - macos-latest - Release` |
| `windows-latest - Boost - TLS - Release` | `Native - windows-latest - Boost - Release` |
| `windows-latest - Standalone - TLS - Release` | `Native - windows-latest - Standalone - Release` |
| `ubuntu-latest - Boost - TLS - ASan+UBSan`; `ubuntu-latest - Standalone - TLS - ASan+UBSan` | `Native - ubuntu-latest - ASan+UBSan` |
| `ubuntu-latest - Boost - TLS - TSan`; `ubuntu-latest - Standalone - TLS - TSan` | `Native - ubuntu-latest - TSan` |

Protection migration requires maintainer-controlled staged acceptance. Keep all
fifteen old requirements while running `workflow_dispatch` against the exact new
branch head. Verify full grouped coverage before changing protection: cold and
populated-cache runs, all twelve configuration conclusions, source/module and
documentation scopes, and failure propagation. Do not change the deployed matrix
or protection during an older release candidate's active acceptance. After the
new grouped evidence is accepted, save the seven mapped native requirements in
place of the twelve old ones, preserving documentation, formatting and the aggregate
gate, then require a fresh full merge candidate containing the latest main. Do not
remove old checks simply to obtain an initial green PR. Record exact revisions,
runs and protection before/after; a green dispatch does not validate the final
merge candidate. Use the actual grouped contexts rather than fake native
compatibility contexts. Changing workflow source alone does not migrate the
repository's protection requirements.

The original-matrix measured baseline for [run 39770711811](https://github.com/OpenArkStudio/arknet/actions/runs/39770711811)
uses candidate `a0dfe09cd543e75cfb5996e537ce2fa49f2a758f`. The local timing snapshot
records independent original profiles; cache hits have not been audited profile
by profile. These minutes regroup their recorded stages for planning, rather than
measuring new grouped jobs. Setup and other job overhead are excluded:

| Planned group | Configure | Build | Test | Recorded stage sum |
| --- | ---: | ---: | ---: | ---: |
| Linux Boost Release, TLS + NoTLS | 6.28 | 37.03 | 6.85 | 50.16 |
| Linux Standalone Release, TLS + NoTLS | 5.45 | 15.20 | 4.38 | 25.03 |
| Windows Boost TLS Release | 9.33 | 33.22 | 6.45 | 49.00 |
| Windows Standalone TLS Release | 11.25 | 20.45 | 4.03 | 35.73 |
| macOS Release, both TLS dependencies | 7.55 | 14.67 | 5.57 | 27.79 |
| Linux AddressSanitizer + UndefinedBehaviorSanitizer, both TLS dependencies | 6.40 | 40.35 | 6.00 | 52.75 |
| Linux ThreadSanitizer, both TLS dependencies | 6.08 | 21.81 | 6.35 | 34.24 |

The rejected five-group serial model combines all four Linux Release profiles:
Build plus Test alone totals 63.46 minutes before Configure or Setup. Combining
the two Windows profiles totals 84.73 recorded stage minutes. That model exceeds the
unchanged 60-minute deadline on this baseline, so Linux Release is split by
dependency and Windows TLS dependencies keep separate jobs.

Seven groups reduce repeated setup where the baseline permits evaluating shared
execution, but these sums are not duration limits or acceptance. The combined
AddressSanitizer/UndefinedBehaviorSanitizer stage sum is 52.75 minutes before
setup; it still needs actual grouped validation. Shared cache/setup may reduce
total runner minutes while serial work increases the longest job or queue-to-gate
wait. Source scope-only successes do not measure full coverage. Hosted cold-cache
and populated-cache runs must compare total runner minutes, longest group,
queue-to-gate completion and per-configuration times at the same revision across
all operating systems. Cache state in this baseline is not assumed. A 60-minute
timeout is failed coverage, not evidence for raising the limit.

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
AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer. Group names include
OS and check mode; named steps distinguish dependency and TLS profiles. Seven
groups, documentation and formatting accompany `CI - required`. Known source
modules on PR branches run their selected native checks in Linux/Boost/TLS/Release;
other cells record scope-only coverage. Code-bearing merge-queue candidates,
manual dispatches run all twelve native configurations. Source PR changes outside
the reviewed module map run the complete Linux Boost/TLS Release suite. Documentation-only candidates run documentation and formatting. A successful
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

Linux Boost and Standalone Asio builds with AddressSanitizer and
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
invalid, inconsistent or insufficient resource observations select one slot. Except
for the Windows Boost Release admission below, other profiles keep their existing parallelism,
and source-module targets, sccache launchers, all twelve matrix profiles, native
tests and the 60-minute job limit remain unchanged. The required gate verifies
that each configuration-specific planning step ran successfully only for the two
intended native profiles inside the sanitizer group.

The helper prints the actual observations, thresholds, selected bound and reason
to the job log and summary. This is a conservative scheduling heuristic: 12 GiB
of available memory is an admission allowance for both slots and their overhead,
not a measured compiler peak or an enforced per-slot limit. The timing metadata
contains no compiler/linker peak memory, and application benchmark RSS cannot
supply it. A successful complete hosted matrix is required before merging this
change; speedup is not established locally. An out-of-memory failure must retain
its logs for correction, without an automatic retry that hides the failure.

Windows Boost TLS Release can experimentally select four compiler slots, using
those same CPU, effective-memory and available-memory gates immediately inside
its build step. Missing, invalid, inconsistent or insufficient observations retain
its existing three slots. Windows Standalone Asio Release and other Release
profiles retain three slots. The build log and step summary record actual resources,
thresholds, selected bound and reason. This decision does not depend on a restored
cache or a configured compiler launcher. All default build targets, examples,
benchmarks, translation units, compiler flags and test commands remain intact;
there is no retry at a lower bound after a build failure.

Validate the selected bound in the actual build log and require all twelve
profiles to complete within the unchanged job budgets. Test archive-cold and
populated-cache behavior separately; a cancelled run is not successful coverage.
Local resource and CMake graph contracts do not measure compiler peak memory,
speedup or hosted duration. Cache hits are not a prerequisite for correctness or
budget acceptance.

API semantics: [Node OS resource APIs](https://nodejs.org/api/os.html#osavailableparallelism),
[constrained memory](https://nodejs.org/api/process.html#processconstrainedmemory),
and [available memory](https://nodejs.org/api/process.html#processavailablememory).

### Native build caches

Every active native group uses the official Mozilla sccache action `v0.0.11`
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
features, default features and the grouped build profile. Boost/TLS combinations
within one group share cache containers and retain separate compiler command hashes. Release,
AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer profiles remain
separate. vcpkg also validates each package ABI before reuse. In `auto` mode,
dependency archives keep their exact primary key and may fall back only to that
identity's `-cold-` prefix. Compiler caches keep their existing identity-specific
prefix plus a unique run suffix so later runs can add objects. No build directory or test result is cached.

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

#### Repeatable cold and populated-cache validation

Manual `workflow_dispatch` offers `cache_mode=auto` (default) or `cold`.
PR and merge-group runs always use `auto`. Invalid modes and nonmanual cold
requests fail validation. Scope-only groups still skip cache setup and native work.
Both manual modes execute all seven groups and twelve profiles with the same
targets, tests, resource admission, scheduling and 60-minute deadline.

`cold` uses fresh archive keys for each run ID, attempt and group identity:
`<identity>-cold-<run-id>-<attempt>-archive`, with no restore-prefix lookup.
The terminating suffix prevents another attempt's key from being a partial match.
The dependency/compiler ABI identity and `SCCACHE_C_CUSTOM_CACHE_BUSTER` stay
unchanged. Both archive families start without restoring prior archives; in-job
reuse across translation units and later profiles remains enabled. This is
archive-cold validation, not an uncached OS, toolchain or source-download test.
No existing cache is deleted. Successful native jobs can save their cold-produced
archives for later `auto` runs, subject to cache availability and ref visibility.

Run the cold matrix first, wait for completion, then run `auto` at the same
unchanged ref and source SHA. For shared default-branch validation:

```sh
gh workflow run ci.yml --repo OpenArkStudio/arknet --ref main -f cache_mode=cold
# After the cold run completes successfully at the recorded SHA:
gh workflow run ci.yml --repo OpenArkStudio/arknet --ref main -f cache_mode=auto
```

Use the intended branch or tag in place of `main` when validating a candidate;
record each run ID and verify both `headSha` values. Respect the existing admission
order and preserve active runs. A successful old run or another head cannot
replace either current-head acceptance run.

In `auto`, existing primary keys remain compatible. Dependency lookup first tries
its unchanged exact key, then the same full identity's `-cold-` prefix; compiler
lookup retains its existing identity prefix, which also includes cold-produced
entries. A prefix restore can succeed while Actions reports `cache-hit=false`:
that flag identifies an exact primary-key match. Inspect each restore action's
matched-key log and vcpkg restore/build counts plus sccache counters; a false flag
alone does not prove a miss, and an archive restore does not prove useful object
hits. Auto may choose an older exact dependency entry before a cold-produced one.
Runner-image/compiler or dependency identity changes can legitimately miss again.

Cold acceptance requires actual archive misses and successful full native/gate
coverage at that head. Populated-cache acceptance requires demonstrated archive
restores and observed package/compiler reuse in the subsequent full run. Do not
label an `auto` run warm without those logs. Cache failures retain normal compiler
fallback, but do not establish populated-cache acceptance. Local key/CLI contracts
do not establish either hosted result, native memory viability or speedup.

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
