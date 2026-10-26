# Production acceptance and ARK adoption

arknet is intended for production use and direct replacement of zephyr in ARK.
A working echo example is only a functional check. Adoption also requires bounded
resources, failure recovery, concurrency checks and regression tests of the consumer.

This page describes current main capabilities and ark adoption. [Release notes](releases.md)
retain their tag boundaries; later main tests, build fixes and framework interfaces
are not additions to v0.2.2.

## Acceptance criteria

| Area | Required evidence |
| --- | --- |
| Protocol correctness | Binary and empty messages, fragmentation, ordering, duplicates, invalid input and independent ARQ/FEC interoperability |
| Lifecycle | Concurrent sends and statistics, callback-triggered stop, reconnect, server restart and completion exactly once |
| Resource limits | Bounded ARQ/FEC/socket queues, server admission limits and explicit overload errors |
| Execution | One context/one thread, one context/multiple threads and multiple contexts/one thread each; endpoint state serialized through strands |
| Platforms | Standalone Asio and Boost.Asio; Linux, macOS and Windows build and test results; separate AddressSanitizer/UndefinedBehaviorSanitizer and ThreadSanitizer checks |
| Performance | Local Release measurements: repeated rounds, payloads, in-flight requests, loss/bursts, throughput, p50/p99 RTT, CPU percentage and wire overhead |
| Sustained operation | Delivery and completion counts, disconnects and retained resources under sustained traffic; short checks must state their duration |
| ARK compatibility | Real zephyr/arknet mixed peers, then ARK framing, event dispatch, session ownership, reconnect and shutdown tests after replacement |
| Deployment | Cross-host traffic on the intended network, configured MTU and socket buffers, representative concurrency and operating duration |
| Identity and access control | RUDP provides no authentication or encryption; validate deployment access control and application identity checks for the target network |

[Testing](testing.md), the [RUDP/FEC report](../performance/rudp.md) and the separate
[RUDP concurrency report](../performance/rudp-concurrency.md) retain their own
measured scopes and results. Local loopback and simulated loss do not establish production
capacity on a deployment network. Deployment acceptance requires the remaining
evidence below.

## Recorded acceptance status

| Scope | Evidence |
| --- | --- |
| Complete local checks, snapshot `6c8418e` | Debug + TLS: Boost.Asio 52 checks / 470 cases / 978,433 assertions; Standalone Asio 51 / 410 / 576,824. Enabled default suites pass without failures or environment skips; archived Zephyr interoperability is not enabled |
| Memory and concurrency, same snapshot | Full Boost.Asio AddressSanitizer+UndefinedBehaviorSanitizer: 52 checks / 470 cases / 615,937 assertions; full Standalone Asio ThreadSanitizer: 51 / 410 / 333,108; no sanitizer diagnostics. Leak checking is disabled and installed-consumer child binaries are not instrumented |
| Real databases | Isolated MySQL Community 8.4.11 with TLS and Redis 7.4.2 with TCP/TLS/mTLS in all four complete configurations; includes client/pool shutdown and publication-failure recovery |
| Additional shutdown coverage | Selected Boost NoTLS targets: 8 checks / 66 cases / 1,515 assertions; AddressSanitizer queued-connect, HTTP external-context and external-stop: 72 process checks / 280 cases / 4,664 assertions |
| Test-only snapshot `25bb9f8` | Four dependency/sanitizer combinations, 30 focused cases each, plus 128 selected native-error executions; all pass locally. The MSVC CRT allocation path is covered by Windows CI, outside this macOS validation |
| Pool-shutdown snapshot `550fb58` | Both dependencies pass the MySQL and Redis shutdown suites under AddressSanitizer+UndefinedBehaviorSanitizer and ThreadSanitizer, including queued acquire cancellation and callback reentry. Unchanged return-failure targets also pass all four configurations; exact counts and source boundaries are in [testing](testing.md) |
| Cross-platform release gate | Published v0.2.2, source `c5432ca`, passed all 14 required checks on its [actual merge candidate](https://github.com/OpenArkStudio/arknet/actions/runs/39034278627); local results do not substitute for release-candidate validation |
| Local performance | Release measurements identify product revision `2aa5bc0`; see the [comparison reports](../performance/index.md). Redis-protocol performance uses Valkey 8.0.11, separately from Redis 7.4.2 correctness checks |
| Real legacy peers, 2026-10-13 | Both dependency configurations: 12 process-pair scenarios, 16 sessions and 512 messages plus echoes each; independent archived evidence |
| Measured RUDP/FEC load | 96 configurations, 276 runs, 7,960,439 successful message round trips; six 120-second sustained runs |
| ark integration | Direct replacement and C++20 in [#38](https://github.com/OpenArkStudio/ark/pull/38); core/official plugin organization in [#41](https://github.com/OpenArkStudio/ark/pull/41), snake_case interfaces/style in [#43](https://github.com/OpenArkStudio/ark/pull/43), and receive buffering in [#42](https://github.com/OpenArkStudio/ark/pull/42), all merged |
| ark public database plugins | Accepted ark `cbb65971` provides optional [MySQL](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/mysql.md) and [Redis](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/redis.md) coroutine modules with owning configuration, the manager shared executor, natural task drain and native-resource retirement contracts; these framework APIs are separate from arknet callback clients |
| Production deployment | Target-network cross-host capacity, production-duration endurance, business acceptance and deployment access control pending |

The released v0.2.2 source has missed the response deadline in a local
5,000-connection TCP Echo workload on macOS 27.0.1: one IO worker per process,
1 KiB payload and four outstanding requests per connection, using either Boost
1.90 or Standalone Asio 1.38.2. Its cause is unconfirmed and this workload has
not passed capacity acceptance. See the [release notes](releases.md).
[PR #26](https://github.com/OpenArkStudio/arknet/pull/26) improves benchmark client
cleanup scheduling on main; it is outside v0.2.2 and does not resolve this TCP
response timeout.

An independent control uses official Asio directly, without arknet endpoints or
runtime code. The retained local diagnostic uses a combination of the
[descriptor-limit](https://github.com/OpenArkStudio/arknet/pull/54) and
[socket-operation](https://github.com/OpenArkStudio/arknet/pull/56) changes;
it is not a released revision.

| Configuration / observation | Result |
| --- | --- |
| Run identity | 2026-10-23; integration source `cd7205b34152a26cca061b947cd33aaa785248d8`; binary SHA-256 `dabd26dd9a2c8dd197fdaac23939ace8e94d5a7b59ea170c9e9aeb32d2d5ae04` |
| Host / build | Mac16,11, 14 CPU cores, 48 GiB, macOS 27.0.1; AppleClang 21 Release, Boost 1.90, TLS off |
| Workload | 5,000 loopback connections; 1 KiB body; up to four in-flight requests per connection; one IO context and one IO worker per process; 3 s traffic hold; 5 s request deadline; 40 s server deadline; composed and underlying socket tracing enabled |
| Completion / cleanup | Both peers reached 5,000 ready/started and 4,999 finished. Connection 759 retained requests 65–68. The client failed at its request deadline; the server then observed a peer reset. Both completed cleanup |
| Actual process limits | Both startup records: `RLIMIT_NOFILE` soft 1,048,575 / hard `"unlimited"`. Sampled FD peaks: client 5,008, server 5,009; zero connect errors |
| Host descriptor settings | Read-only snapshots: `kern.maxfilesperproc` 184,320, `kern.maxfiles` 368,640. These are distinct from the process's reported resource limits and do not establish connection capacity |
| Stalled client | Completed socket/composed read bytes 69,954; write bytes 74,318. An underlying read with 4,096-byte capacity remains pending; no active write, queued application send or kernel-readable bytes |
| Server for that connection | Completed socket/composed read and write bytes both 74,318; response 68 completed its write. The following read completed with a peer reset after the client's timeout |
| TCP observation | Client send-buffer bytes 0; server send-buffer bytes 4,364, including possible in-flight data. Both send windows are positive. A completed write does not establish peer delivery |
| Header capture | Three retained files; 1,967,917 packets captured; tcpdump reported zero packets dropped by the kernel. Interface-drop and capture-exit status were not retained; packet absence alone is not loss proof |
| Captured TCP progress | Both directions have header-declared payload coverage `[0,74318)`. The largest observed cumulative ACK is 74,318 client→server and 69,954 server→client. Client SACKs report later ranges `[71045,72136)` and `[73227,74318)` without advancing that cumulative boundary |
| Recorder / worker progress | All 5,000 sessions recorded; the incomplete session emitted per peer without omitted/truncated output or invalid transitions. Both peers completed 13/13 worker probes; this does not prove per-socket readiness |

The unresolved response gap is four 1,091-byte frames between server write
completion and client read completion. Socket and composed lifetime byte totals
agree. A 2,182-byte underlying read followed by a zero-byte buffered completion
is valid; bounded event tails are not complete histories. No framing,
handler-association or application-queue defect is proved.

The first unacknowledged range `[69954,71045)` appears in three server payload
headers. The client cumulative ACK remains 69,954 through its reset, about
4.96 capture-clock seconds after the final server payload header. Captured
headers do not prove that the receiving TCP stack accepted those bytes; payload
and checksums were not verified. The reset server's missing remote endpoint is
associated using the unique session identity and the client's observed tuple,
with matching server-local address; that derived association is a coverage limit.

The root cause remains unresolved. Frozen operation counters have no verified
absolute capture-clock mapping, and packet headers do not establish socket
readiness or callback dispatch. The macOS netstat manual documents that non-root
TCP statistics return zeros. The retained unprivileged counters therefore cannot
establish an absence of retransmissions or resource drops. This failed diagnostic is not performance or capacity acceptance.
Raw logs and captures stay local. See [connection benchmarks](../../benchmarks/CONNECTIONS.md)
for parameters.

The native control also offers default-off `--tcp-kernel-samples on` with
`--operation-trace on` to retain periodic public Apple `TCP_CONNECTION_INFO`
queries before failure cleanup. The existing one-second worker marker checks
unfinished traffic sessions with DATA received and at least one second without a
composed read/write completion. It keeps eight samples for at most 16 identities
in less than 32 KiB of extra fixed storage; recovered/completed/closed sessions
release slots, while persistent stalls keep theirs. Omission and overwrite are
explicit. Other platforms return null with an unsupported reason. Sample time is
separate from the operation-ring freeze; delayed sampling can miss transient
states. Selected Boost and Standalone Asio regressions include a controlled
post-DATA stall that retains an ESTABLISHED sample before timeout cleanup. See
[usage and limits](../../benchmarks/CONNECTIONS.md#opt-in-tcp-kernel-samples).

### Frame-boundary diagnostics

The native control's pre-cancel `connection_diagnostic` includes a bounded
`frame_boundary` object. Its `status` distinguishes `empty`, `incomplete_prefix`,
`invalid_prefix`, `incomplete_frame` and `complete_frame`. `input_frame_ready` is
true only for a complete frame with a canonical, allowed length prefix; an invalid
prefix no longer appears ready. This is framing validation, not verification of
the application header or payload.

`prefix` contains at most nine unsigned byte values and excludes payload bytes.
`buffered_bytes` counts all bytes in the current receive buffer, including any
following frames. `declared_body_bytes` is null until the complete prefix is
available; it retains decoded lengths even when they are invalid or oversized.
`required_wire_bytes` includes the first frame's prefix, and `missing_frame_bytes`
is its remaining byte count, clamped at zero when following bytes are buffered.
Both fields are null for empty, incomplete-prefix or invalid-prefix states; no
addition is performed on an unvalidated length.

The matcher and its 4,096-byte wire cap are unchanged. A canonical nine-byte
prefix declares at least 65,536 body bytes, so it exceeds this cap and reports
`invalid_prefix`; partial nine-byte prefixes remain `incomplete_prefix`.
The native `--self-test` checks these states, framing limits, extreme 64-bit
lengths and JSON output using fixed byte strings, without connection load.
This diagnostic improvement neither explains nor fixes the 5,000-connection
stall. It changes no protocol, IO operation, deadline or workload, and adds no
capacity or performance acceptance evidence.

### Kernel samples during stalled connections

A fixed five-run diagnostic series used the same host and workload above with
sampling enabled: Boost 1.90 Release, TLS off, native source
`605625dfea0efff90468771cc0839d03866a134c` (series revision `9200bfb` only adds
documentation). All 5,000 connections reached ready/started in each run. These
consecutive outcomes are not an estimated failure probability.

| Run | Finished connections per peer | Unfinished connections per peer |
| --- | ---: | ---: |
| 1 | 4,992 | 8 |
| 2 | 4,996 | 4 |
| 3 | 5,000 | 0 |
| 4 | 4,999 | 1 |
| 5 | 4,995 | 5 |

All ten processes completed cleanup with zero connect errors. The 18 unfinished
identities match across peers; 36 histories retain 211 kernel samples. Every
retained client sample reports:

| Public TCP observation | Value |
| --- | --- |
| Connection state / recovery flag | ESTABLISHED / loss recovery |
| Congestion window | 0 bytes |
| Occupied send socket buffer, including in-flight data | 4,364 bytes |
| Peer receive window | 293,184–521,024 bytes |
| Exported retransmission timeout | 0 ms |

The client's completed writes exceed the server's completed reads by four
1,091-byte frames on every matched identity. IO worker probes continue completing;
no final unfinished identity or output was omitted. Transient eligible
observations were omitted and older server samples overwritten. Server sample
tails occur after client cleanup, so the peer sample windows are not simultaneous.
Client histories begin at least one second after completion progress stops and
do not show the onset of recovery. Successful connections emit no comparable
history, and this series does not isolate instrumentation timing effects.

The installed SDK defines these flags and units. Its exported zero timeout does
not establish the installed kernel's armed timers or deadlines. The observations
focus the investigation on TCP recovery state; they do not establish an Asio or
kernel defect, explain the captured failure, or pass capacity acceptance. Raw
samples remain local.

The `resource_startup` descriptor diagnostics read each running process's
`nofile_soft` and `nofile_hard` through `getrlimit(RLIMIT_NOFILE)`. Finite limits are integers,
infinity is `"unlimited"`, and unavailable values are null with a reason;
Windows retains handle counts and reports this POSIX limit as not applicable.
Limits include files, pipes and event-loop descriptors as well as sockets.
Current shell, launch-service and macOS `kern.maxfiles*` settings are not evidence
of historical process limits or portable capacity defaults. Earlier records
without startup limits retain that evidence gap. The tools change no limits,
workloads or deadlines; cross-host and production capacity remain pending.

[Testing and validation](testing.md) records configurations, source snapshots and
sanitizer limitations. Concurrent complete owner stops are serialized for TCP
servers and HTTP/WebSocket/TLS-derived servers; this is not a blanket guarantee
for every protocol. Hosts must stop producers and keep external context runners
alive through drain. Local validation does not establish dynamic-library
unloading safety or the cluster PCU target.

The [RUDP/FEC record](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec)
retains its independent interoperability evidence. The ark replacement is merged;
business behavior and deployment acceptance still need their own workload tests.

## Framework coroutine and database boundary

arknet supplies native callback clients, pools and the standard Asio executor from
`manual_runtime`; it has no dedicated public database `co_await` facade. The separate
ark framework provides `mysql_module` and `redis_module`, owning `configure()` options
and manager-owned task groups. arknet still runs native IO. Module updates dispatch
completions; the manager polls its shared business executor centrally and resumes
consumer coroutines on its owner thread. See ark's [MySQL](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/mysql.md),
[Redis](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/redis.md) and [task lifecycle](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/guides/tasks.md) guides.

Natural manager stop first seals top-level task admission, lets accepted workflows
submit their remaining steps, then retires native resources from the host lifecycle
frame. A zero task count does not establish callback/thread drain or safe library
unloading. Public-module protocol-peer regressions, plugin DSO loading checks and
explicit real-service programs have different scopes. `check-database` compiles the
real-service programs without connecting them to databases. The [ark database test
guide](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/tests/database/README.md) records prerequisites and limits. Compilation
and fixture checks do not substitute for explicit real-service execution or
establish arbitrary DSO unload, hot-update or production capacity acceptance.

## Deployment capacity target

ARK uses multiple processes, targeting **100,000-200,000 peak concurrent users
(PCU) across the cluster**. PCU counts online users. Count TCP connections,
WebSocket connections and RUDP sessions separately: one user may hold several
connections, and service-to-service connections also consume resources.

The following evenly distributed estimates are planning examples. They do not
select a deployment size or report measured capacity.

| Ingress processes | Users per process at 100,000 PCU | Users per process at 200,000 PCU |
| --- | ---: | ---: |
| 16 | 6,250 | 12,500 |
| 32 | 3,125 | 6,250 |
| 64 | Approximately 1,563 | 3,125 |

Capacity acceptance needs these workload parameters before testing on the intended
hosts and cross-host network:

- Connection mix by protocol, connections per user, messages per connection and payload distribution.
- CPU, memory, threads and admission limits per process, plus operating-system file descriptor limits and socket buffers.
- Login and reconnect bursts, including the connections and traffic remaining processes must absorb after an ingress process fails.
- Operating duration and acceptance thresholds for p99 RTT, throughput, CPU percentage, memory growth, errors and resource rejection.

The RUDP server defaults to **1024 sessions** through `set_max_sessions()`. Set an
explicit limit from measured per-process capacity, together with ARQ/FEC and send
queue limits. Raising the session limit alone does not validate capacity. Current
local reports cover message transport and recovery; they do not establish
100,000-200,000 cluster PCU capacity.

## ARK capability mapping

| Current use | arknet replacement | Required behavior |
| --- | --- | --- |
| TCP client/server/session | `tcp_client`, `tcp_server`, `tcp_session` | Raw byte streams; preserve ARK's own AFCS/AFSS framing |
| WebSocket client/server/session | `ws_client`, `ws_server`, `ws_session` | Binary messages and upgrade callbacks |
| UDP with `use_kcp` for RDP | `rudp_client`, `rudp_server`, `rudp_session` | Legacy ARQ-only wire mode for existing zephyr peers |
| HTTP client | `http_request` | Completion dispatched by `http_client_module_impl::update()`, with existing status handling |
| Optional HTTP/Consul modules | HTTP requests, server and router | Update method/route APIs and verify registered routes when enabled |

ARK bounds application bodies to 4096 bytes and performs its own application
framing. Transport fragmentation is below that layer. Store ARK's application
session ID in `user_data`; it is distinct from the RUDP conversation ID.

## Replacement rules

- ARK uses C++20; direct transport callback replacement and framework database coroutines follow their separate lifecycle contracts.
- Replace endpoint and callback types directly, remove zephyr includes and its build compatibility overlay.
- Use `async_send()` for ARK's asynchronous send admission. arknet `send()` waits and returns a byte count.
- Handle asynchronous completion errors as well as the admission boolean. An admitted RUDP send can later fail its ARQ quota; dispatch the failure through ARK's update/event path and define the retry or rejection policy.
- Connect/disconnect/start/stop callbacks take no error argument; capture `get_last_error()` at callback entry. RUDP handshake callbacks retain the `error_code` argument.
- Transfer constructor, silence timeout, buffer and reconnect settings by meaning. The positional parameters differ.
- Preserve ARK's event queue and update-thread dispatch; do not execute business callbacks on IO threads.
- Set ARQ parameters explicitly for the first rollout: zephyr used MTU 1400 with congestion control disabled, while arknet defaults to 1200 with congestion control enabled.
- Keep FEC opt-in. A FEC-capable server accepts legacy ARQ sessions; a FEC client requires negotiation support from its peer.

The direct ARK replacement is merged. Run the business regression, cross-host
capacity and production-duration checks before a production rollout.
