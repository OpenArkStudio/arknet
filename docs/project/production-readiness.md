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
capacity on a deployment network. RUDP is implemented and locally validated in
`v0.1.0`; deployment acceptance requires the remaining evidence below.

## Recorded acceptance status

| Scope | Evidence as of 2026-10-13 |
| --- | --- |
| Complete local checks | Both Release + TLS backends: 55 checks and 219 doctest cases each |
| Concurrency and memory checks | Selected ASan/UBSan suites: 109 cases; selected TSan suites: 86 cases |
| Cross-platform CI | [All 13 jobs passed](https://github.com/OpenArkStudio/arknet/actions/runs/38355205095): both Asio backends, Linux/macOS/Windows, builds without TLS, ASan/UBSan, TSan, installed consumers and formatting |
| Real legacy peers | Both backends: 12 process-pair scenarios, 16 sessions and 512 messages plus echoes each |
| Measured RUDP/FEC load | 96 configurations, 276 runs, 7,960,439 successful message round trips; six 120-second sustained runs |
| ARK replacement | Capability mapping and migration rules defined; direct replacement and business regression pending |
| Production deployment | Target-network cross-host capacity, production-duration endurance and deployment access control pending |

[Retained logs and commands](https://github.com/OpenArkStudio/arknet/tree/main/tests/results/macmini-m4pro-20261013/rudp-fec) identify exact coverage. Local completion is a gate for ARK integration, not a claim that deployment acceptance is complete.

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
| HTTP client | `http_request` | Completion dispatched by the module's `Update()`, with existing status handling |
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

Complete arknet acceptance first. Then perform the direct ARK replacement and its
business regression tests before a production rollout.
