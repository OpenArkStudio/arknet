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

| Scope | Evidence as of 2026-10-15 |
| --- | --- |
| Complete local checks | Release + TLS: Boost.Asio 75 checks / 374 cases / 1,952,026 assertions; Standalone Asio 62 / 314 / 976,980; no failures or skipped cases |
| Database correctness | Real MySQL 8.4.11 and Redis 7.4.2; MySQL client/pool 51 cases / 1,899 assertions, Redis 35 / 316 |
| Database memory and concurrency | Boost.Asio database TSan: 86 cases / 2,215 assertions; MySQL Standalone Asio TSan and Boost.Asio ASan+UBSan: 51 / 1,899 each; Redis Standalone Asio ASan+UBSan: 35 / 316; no failures, skips, diagnostics or suppressions; [scope and commands](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261015/database-clients/README.md) |
| RUDP memory/concurrency evidence, 2026-10-13 | Selected ASan/UBSan suites: 109 cases; selected TSan suites: 86 cases; not a complete current-project sanitizer run |
| Cross-platform CI, v0.1.0 historical evidence | [13 jobs passed](https://github.com/OpenArkStudio/arknet/actions/runs/38355205095): both Asio dependency configurations, Linux/macOS/Windows, builds without TLS, ASan/UBSan, TSan, installed consumers and formatting; this run does not validate v0.2.0 |
| Cross-platform release gate | All 14 required checks must pass before merge: [CI runs](https://github.com/OpenArkStudio/arknet/actions/workflows/ci.yml); local results do not substitute for the release candidate's own checks |
| Real legacy peers, 2026-10-13 | Both dependency configurations: 12 process-pair scenarios, 16 sessions and 512 messages plus echoes each |
| Measured RUDP/FEC load | 96 configurations, 276 runs, 7,960,439 successful message round trips; six 120-second sustained runs |
| ark integration | Direct replacement and C++20 in [#38](https://github.com/OpenArkStudio/ark/pull/38); core/official plugin organization in [#41](https://github.com/OpenArkStudio/ark/pull/41) and snake_case interfaces/style in [#43](https://github.com/OpenArkStudio/ark/pull/43), all merged |
| ark receive buffering | Allocation changes and local measurements merged in [#42](https://github.com/OpenArkStudio/ark/pull/42); business regression and deployment acceptance remain separate |
| Production deployment | Target-network cross-host capacity, production-duration endurance and deployment access control pending |

[Testing and validation](testing.md) records complete checks and database results.
The [RUDP/FEC record](https://github.com/OpenArkStudio/arknet/tree/main/tests/results/macmini-m4pro-20261013/rudp-fec)
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
