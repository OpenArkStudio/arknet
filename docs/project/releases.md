# Releases

## 0.2.0

This release adds native single-node MySQL and Redis clients, simplifies networking dependencies, and fixes owned-runtime cleanup at process exit. arknet remains C++20 and header-only.

### Database clients

- **MySQL:** text queries, typed prepared parameters/results, explicit transactions, `caching_sha2_password` and `mysql_native_password`, verified TLS, and bounded pools with exclusive leases.
- **Redis:** binary-safe RESP2/RESP3 commands, ordered pipelines, authentication, TLS/mTLS, separate RESP3 push delivery, and bounded pools with exclusive leases.
- **Request control:** bounded admission and results, deadlines, cancellation, session reset, and explicit indicators when a failed command may already have executed. Commands are not automatically replayed.

The [database client guide](../features/database-clients.md) links complete client and pool programs, configuration and limitations. Include the corresponding MySQL or Redis public headers explicitly. Both clients support Boost.Asio and Standalone Asio.

MySQL pools restore their configured database after reset; an empty configured database causes returned connections to be discarded. Statements remain session-bound. Redis pools reset the session and repeat their configured protocol, authentication and database setup before reuse. A lease remains exclusive while pending work and return processing finish.

OpenSSL is required for MySQL authentication even when TLS is explicitly disabled; enable `ARKNET_ENABLE_SSL` for the MySQL connection API. MySQL requires verified TLS by default and does not downgrade automatically. Plain Redis does not require OpenSSL; secure Redis does.

### Dependencies and compatibility

| Configuration | Available capabilities |
| --- | --- |
| Boost.Asio + Boost.Beast, default | All supported transports and database clients; secure capabilities require OpenSSL |
| Standalone Asio | TCP, UDP, RUDP and database clients; TLS requires OpenSSL |

`ARKNET_USE_BOOST_ASIO` now defaults to `ON`. Set it to `OFF` to retain Standalone Asio. HTTP/HTTPS and WebSocket/WSS require Boost; the bundled BHO tree has been removed. Applications using Standalone Asio for those protocols must select Boost. Includes under `arknet/bho/` and direct uses of bundled BHO types must migrate to official Boost APIs.

The `arknet::arknet` interface target propagates compile and dependency requirements; it does not introduce an arknet binary library. See [installation](../get-started/installation.md) before upgrading an existing build or installed package.

### Runtime and deadline fixes

Owned-runtime process cleanup now waits for callbacks, worker threads and TLS resources before OpenSSL's normal cleanup. This covers last-owner release from a callback, nested owned pools, and supported direct or indirect global owners. The cleanup worker starts with the first owned IO pool; including a header alone does not start a worker.

Each translation unit declaring a global owner must include the relevant IO header before that declaration. Unrelated forward-declared or type-erased global owners without that include require explicit teardown before `main` returns. Explicit or pre-included legacy Asio thread-storage policies retain their own teardown requirements. See [process shutdown](../concepts/lifecycle.md#process-shutdown).

Database deadline checks reject nonpositive and unrepresentable durations before clock arithmetic. Documented zero per-call overrides continue to select the configured default. Queue admission is rolled back if posting accepted work throws.

### Validation and scope

Client and pool correctness suites contain 49 MySQL cases / 1355 assertions and 35 Redis cases / 316 assertions. Independent-server checks include MySQL Community Server 8.4.11 and Redis 7.4.2. Protocol codecs, clock boundaries and process-exit ordering have separate tests; sanitizer results and local Release measurements are recorded in [testing](testing.md) and the [performance reports](../performance/index.md).

MySQL supports one Classic Protocol result per command, without multi-result execution, `LOCAL INFILE`, compression, cursors or arbitrary authentication plugins. Redis Cluster, Sentinel and Pub/Sub remain TODO. Public database coroutine APIs, HTTP/2, HTTP/3, named pipes, Unix domain sockets and RPC are separate [roadmap](roadmap.md) items.

Application and ark plugin regression, cross-host load and deployment endurance remain [deployment acceptance](production-readiness.md); this release does not establish production PCU capacity.

## 0.1.0

The first regular release includes TCP, native UDP, RUDP, WebSocket/WSS,
HTTP/HTTPS with routing, and optional TLS/mTLS. arknet remains C++20 and
header-only, with Standalone Asio and Boost.Asio dependency configurations.

RUDP includes ARQ-only, fixed Reed–Solomon FEC and adaptive FEC policies.
Negotiation, ordered delivery, recovery, bounded resources, reconnect and
legacy zephyr interoperability have passed local checks. Single HTTP requests
include deadlines and cancellation.

Lifecycle status queries use atomic state, avoiding races with socket and
acceptor shutdown. A permanent regression polls clients, servers and retained
sessions while their IO thread starts and closes transports.

The [testing record](testing.md) and [RUDP performance report](../performance/rudp.md)
describe the verified workloads and source revisions. A library release does
not establish a deployment's capacity: cross-host load, production-duration
endurance and ARK business regression remain [deployment acceptance](production-readiness.md).
Cluster capacity of 100,000–200,000 PCU has not been measured.

Changes to `main` require a pull request, passing required CI checks and resolved
review conversations. Administrators follow the same protection. Contributions
are squash-merged with English commit subjects; another maintainer's approval
is currently optional.

[GitHub releases](https://github.com/OpenArkStudio/arknet/releases)
