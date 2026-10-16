# Releases

## 0.2.1

This release improves shutdown reliability for database clients and TCP-based protocols, and adds application-driven IO execution.

### Features

- **Database shutdown waits:** MySQL connections, Redis clients and both pools provide `close_and_wait()`. Permanent closure rejects new requests and waits for accepted operations, callbacks and owned IO resources to retire. Retained leases and statement handles become inert.
- **Application-driven execution:** `manual_runtime` lets an application drive IO with `poll()` from its update loop, without creating a worker that executes business handlers. Its standard Asio executor supports `awaitable` and `co_spawn`; dedicated public coroutine protocol APIs remain TODO.

### Fixes

- TCP, TLS TCP, HTTP and WebSocket shutdown waits cover outstanding IO and callbacks, preventing endpoint resources from being released while still in use. Shutdown also prevents long timer rearming; explicit restart and normal reconnect remain available.
- Database pools complete queued connection requests during shutdown without relying on container allocations, including reentrant close and cancellation. Redis pools preserve diagnostics and recover connection capacity after lease-return submission failures, without replaying commands or changing successful results.

### Upgrade notes

Stop application producers before waiting for shutdown, and keep external contexts running until completion. Network shutdown does not drain business tasks or establish dynamic-plugin unload safety. See [lifecycle](../concepts/lifecycle.md) and [database shutdown](../features/database-clients.md#limits-and-callback-ownership).

The [five-second workload comparison](../performance/steady-workloads.md) records 6.51% lower median throughput and 13.29% higher P99 for the 4 KiB WebSocket case. Five-run throughput ranges overlap; the cause is unconfirmed. Measurements use snapshot `2aa5bc0`, before the release's final pool-shutdown correction. Detailed [validation](testing.md), [performance reports](../performance/index.md) and release attachments retain their original source identities; deployment capacity has not been established.

## 0.2.0

This release adds native single-node MySQL and Redis clients, makes Boost the default networking dependency, and improves process-exit cleanup. arknet remains C++20 and header-only.

### Features

- **MySQL:** text queries, typed prepared parameters and results, explicit transactions, `caching_sha2_password` and `mysql_native_password`, verified TLS, and bounded connection pools with exclusive leases.
- **Redis:** binary-safe RESP2/RESP3 commands, ordered pipelines, authentication, TLS/mTLS, separate RESP3 push delivery, and bounded connection pools with exclusive leases.
- **Request control:** queue and result limits, deadlines, cancellation, session reset, and explicit indication when a failed command may already have executed. Commands are not automatically replayed.

Both clients support Boost.Asio and Standalone Asio. The [database client guide](../features/database-clients.md) provides complete programs, configuration and pool reuse rules. MySQL pools restore the configured database after reset, or discard returned connections when no database is configured; prepared statements remain session-bound. Redis pools restore protocol, authentication and database settings before reuse. Leases remain exclusive until pending work and return processing finish.

### Fixes

- Owned-runtime process cleanup waits for callbacks, worker threads and TLS resources before OpenSSL's normal cleanup, including supported callback and global-owner lifetimes. The cleanup worker starts with the first owned IO pool; including a header alone does not start one.
- Database requests reject invalid or unrepresentable deadlines before clock arithmetic and restore queue capacity if submission throws. Documented zero per-call overrides still select the configured default.
- MySQL retains its runtime through asynchronous submission and releases command timers before the runtime, including when a completion destroys the connection.

### Upgrade notes

| Dependency configuration | Available capabilities |
| --- | --- |
| Boost.Asio + Boost.Beast, default | All supported transports and database clients; secure capabilities require OpenSSL |
| Standalone Asio | TCP, UDP, RUDP and database clients; TLS requires OpenSSL |

`ARKNET_USE_BOOST_ASIO` now defaults to `ON`; set it to `OFF` to retain Standalone Asio. HTTP/HTTPS and WebSocket/WSS require Boost. The bundled BHO tree has been removed: replace `arknet/bho/` includes and bundled BHO types with official Boost APIs. The `arknet::arknet` interface target propagates headers, settings and dependencies without an arknet binary library. Review [installation](../get-started/installation.md) when upgrading.

MySQL authentication requires OpenSSL even with TLS disabled, and its connection API requires `ARKNET_ENABLE_SSL`. Verified TLS is the default; there is no automatic downgrade. Plain Redis does not require OpenSSL; secure Redis does.

Each translation unit declaring a global IO owner must include the relevant arknet IO header before that declaration. Owners without that include, including forward-declared or type-erased globals, need explicit teardown before `main` returns. Legacy Asio thread-storage configurations retain their own requirements. See [process shutdown](../concepts/lifecycle.md#process-shutdown).

MySQL supports one Classic Protocol result per command; multi-result execution, `LOCAL INFILE`, compression, cursors and arbitrary authentication plugins are outside this release. Redis Cluster, Sentinel and Pub/Sub, dedicated database coroutine APIs and other planned protocols remain on the [roadmap](roadmap.md).

See [testing](testing.md) for independent MySQL and Redis server checks and sanitizer coverage, and [performance](../performance/index.md) for local measurements. Application and ark plugin regression, cross-host load and deployment endurance remain [deployment acceptance](production-readiness.md); this release does not establish production PCU capacity.

## 0.1.0

The first regular release provides a C++20 header-only networking library with client/server APIs, CRTP extension points and component composition.

### Features

- TCP, native UDP, RUDP, WebSocket/WSS, HTTP/HTTPS with routing, optional TLS/mTLS, and IPv4/IPv6.
- RUDP with ARQ-only, fixed Reed–Solomon FEC and adaptive FEC policies. Local checks cover negotiation, ordered delivery, recovery, resource limits, reconnect and legacy zephyr interoperability.
- Automatic reconnect, timers and `post`, bounded sends, owned or external IO contexts, and deadlines and cancellation for single HTTP requests.

### Fixes

Lifecycle status queries use atomic state to avoid races with socket and acceptor shutdown, including concurrent queries of retained sessions.

### Upgrade notes

This version defaults to Standalone Asio and also supports Boost.Asio. The default changes to Boost in 0.2.0.

[Testing](testing.md) and [RUDP performance](../performance/rudp.md) identify verified workloads and source revisions. Cross-host load, production-duration endurance and ark business regression remain [deployment acceptance](production-readiness.md). Cluster capacity of 100,000–200,000 PCU has not been measured.

[GitHub releases](https://github.com/OpenArkStudio/arknet/releases)
