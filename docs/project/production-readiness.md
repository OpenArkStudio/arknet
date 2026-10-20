# Production acceptance and ARK adoption

arknet is intended for production use and direct replacement of zephyr in ARK.
A working echo example is only a functional check. Adoption also requires bounded
resources, failure recovery, concurrency checks and regression tests of the consumer.

## Acceptance criteria

| Area | Required evidence |
| --- | --- |
| Protocol correctness | Binary and empty messages, fragmentation, ordering, duplicates, invalid input and independent ARQ/FEC interoperability |
| Lifecycle | Concurrent sends and statistics, callback-triggered stop, reconnect, server restart and completion exactly once |
| Resource limits | Bounded ARQ/FEC/socket queues, server admission limits and explicit overload errors |
| Execution | One context/one thread, one context/multiple threads and multiple contexts/one thread each; endpoint state serialized through strands |
| Platforms | Standalone Asio and Boost.Asio; Linux, macOS and Windows build and test results; separate ASan/UBSan and TSan checks |
| Performance | Local Release measurements: repeated rounds, payloads, in-flight requests, loss/bursts, throughput, p50/p99 RTT, CPU percentage and wire overhead |
| Sustained operation | Delivery and completion counts, disconnects and retained resources under sustained traffic; short checks must state their duration |
| ARK compatibility | Real zephyr/arknet mixed peers, then ARK framing, event dispatch, session ownership, reconnect and shutdown tests after replacement |
| Deployment | Cross-host traffic on the intended network, configured MTU and socket buffers, representative concurrency and operating duration |
| Identity and access control | RUDP provides no authentication or encryption; validate deployment access control and application identity checks for the target network |

[Testing](testing.md) and the [RUDP report](../performance/rudp.md) record measured
scope and results. Local loopback and simulated loss do not establish production
capacity on a deployment network. Deployment acceptance requires the remaining
evidence below.

## Recorded acceptance status

| Scope | Evidence |
| --- | --- |
| Complete local checks, snapshot `6c8418e` | Debug + TLS: Boost.Asio 52 checks / 470 cases / 978,433 assertions; Standalone Asio 51 / 410 / 576,824. Enabled default suites pass without failures or environment skips; archived Zephyr interoperability is not enabled |
| Memory and concurrency, same snapshot | Full Boost.Asio ASan+UBSan: 52 checks / 470 cases / 615,937 assertions; full Standalone Asio TSan: 51 / 410 / 333,108; no sanitizer diagnostics. Leak checking is disabled and installed-consumer child binaries are not instrumented |
| Real databases | Isolated MySQL Community 8.4.11 with TLS and Redis 7.4.2 with TCP/TLS/mTLS in all four complete configurations; includes client/pool shutdown and publication-failure recovery |
| Additional shutdown coverage | Selected Boost NoTLS targets: 8 checks / 66 cases / 1,515 assertions; ASan queued-connect, HTTP external-context and external-stop: 72 process checks / 280 cases / 4,664 assertions |
| Test-only snapshot `25bb9f8` | Four dependency/sanitizer combinations, 30 focused cases each, plus 128 selected native-error executions; all pass locally. The MSVC CRT allocation path is covered by Windows CI, outside this macOS validation |
| Pool-shutdown snapshot `550fb58` | Both dependencies pass the MySQL and Redis shutdown suites under ASan+UBSan and TSan, including queued acquire cancellation and callback reentry. Unchanged return-failure targets also pass all four configurations; exact counts and source boundaries are in [testing](testing.md) |
| Cross-platform release gate | Published v0.2.2, source `c5432ca`, passed all 14 required checks on its [actual merge candidate](https://github.com/OpenArkStudio/arknet/actions/runs/39034278627); local results do not substitute for release-candidate validation |
| Local performance | Release measurements identify product revision `2aa5bc0`; see the [comparison reports](../performance/index.md). Redis-protocol performance uses Valkey 8.0.11, separately from Redis 7.4.2 correctness checks |
| Real legacy peers, 2026-10-13 | Both dependency configurations: 12 process-pair scenarios, 16 sessions and 512 messages plus echoes each; independent archived evidence |
| Measured RUDP/FEC load | 96 configurations, 276 runs, 7,960,439 successful message round trips; six 120-second sustained runs |
| ark integration | Direct replacement and C++20 in [#38](https://github.com/OpenArkStudio/ark/pull/38); core/official plugin organization in [#41](https://github.com/OpenArkStudio/ark/pull/41), snake_case interfaces/style in [#43](https://github.com/OpenArkStudio/ark/pull/43), and receive buffering in [#42](https://github.com/OpenArkStudio/ark/pull/42), all merged |
| ark database development | [#46](https://github.com/OpenArkStudio/ark/pull/46) adds private native services, Asio awaitables and a library-owned runtime driven from the module thread; [#49](https://github.com/OpenArkStudio/ark/pull/49) adds the standalone callback/coroutine benchmark. Formal database-plugin activation remains separate |
| Production deployment | Target-network cross-host capacity, production-duration endurance, business acceptance and deployment access control pending |

The released v0.2.2 source has missed the response deadline in a local
5,000-connection TCP Echo workload on macOS 27.0.1: one IO worker per process,
1 KiB payload and four outstanding requests per connection, using either Boost
1.90 or Standalone Asio 1.38.2. Its cause is unconfirmed and this workload has
not passed capacity acceptance. See the [release notes](releases.md).
[PR #26](https://github.com/OpenArkStudio/arknet/pull/26) improves benchmark client
cleanup scheduling on main; it is outside v0.2.2 and does not resolve this TCP
response timeout.

An independent control using official Asio directly also exhibited an incomplete
target, without arknet endpoints or runtime code:

| Control configuration | Observed result |
| --- | --- |
| Diagnostic source `c945ce9`, Boost 1.90, AppleClang 21 Release, TLS off; Mac16,11, 14 CPU cores, 48 GiB, macOS 27.0.1; measured 2026-10-20 | 5,000 connections established, 4,999 finished, four unanswered requests on one connection; both processes completed cleanup |
| One IO worker, 1 KiB payload, four outstanding requests per connection; response deadline 5 s, server deadline 40 s | Client write queue empty; 4,364 bytes remained in its kernel send buffer; application buffers and kernel-readable bytes were zero on both peers |
| Same failed connection, before socket cancellation | Client send window 366,912 bytes; 19,032 retransmitted bytes and 4,364 out-of-order bytes; `rto_ms` was zero |
| Correlated packet headers | Receiver ACK remained at 602375564 with a SACK block above the missing 1,091-byte range; the capture includes a retransmission of that range |

This symptom can therefore occur outside arknet's endpoint/runtime implementation.
The failure shows a TCP sequence gap with available window space, rather than
buffered application frames waiting for dispatch. It does not establish a common
cause for the arknet and native-control failures or identify why TCP recovery
stalled. The capture recorded 1,675,186 packets and dropped 364,705; absent packets
cannot be treated as proof that no transmission occurred. All packet headers and
raw logs remain local. Capacity acceptance and cross-host validation are pending.
The reusable control and reproduction parameters are described in
[connection benchmarks](../../benchmarks/CONNECTIONS.md).

[Testing and validation](testing.md) records configurations, source snapshots and
sanitizer limitations. Concurrent complete owner stops are serialized for TCP
servers and HTTP/WebSocket/TLS-derived servers; this is not a blanket guarantee
for every protocol. Hosts must stop producers and keep external context runners
alive through drain. Local validation does not establish dynamic-library
unloading safety or the cluster PCU target.

The [RUDP/FEC record](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec)
retains its independent interoperability evidence. The ark replacement is merged;
business behavior and deployment acceptance still need their own workload tests.

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

- Raise ARK's minimum standard to C++20, without a coroutine or language-feature rewrite.
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
