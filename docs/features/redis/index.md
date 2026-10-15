# Redis

arknet provides a native, header-only single-node Redis client and connection pool. Commands and replies use arknet types; no Boost.Redis or other Redis driver is required. Both Boost.Asio and Standalone Asio are supported. TLS adds OpenSSL through `ARKNET_ENABLE_SSL`.

| Area | API and behavior |
| --- | --- |
| Connections | `redis::client`, `redis::tls_client`; explicit host and port, authentication and database selection |
| Protocol | RESP2 and RESP3; RESP3 `HELLO` negotiation by default |
| Commands | Binary-safe argument arrays, single commands and ordered pipelines |
| Replies | Owned typed values, nested collections, server errors and RESP3 attributes |
| Pushes | A separate push callback; pushes do not consume command response slots |
| Pools | `redis::pool`, `redis::tls_pool`; bounded connections and pending acquisitions, exclusive move-only leases |
| Failure handling | Deadlines, cancellation, queue limits and explicit unknown write outcomes; no automatic replay |
| TLS | Certificate and host verification, optional client certificates for mTLS |

## Get started

- [Client and pool examples](usage.md): compile and run complete programs against your Redis endpoint.
- [Configuration and limits](configuration.md): authentication, typed replies, cancellation and lease isolation.
- [Performance](performance.md): workload dimensions, units and the independent C++ benchmark.

The library owns IO contexts and threads. A standalone client uses one context and thread; clients created by a pool share that pool's context and thread. Applications submit commands without starting or stopping those contexts themselves.

## Scope

This module targets a single configured Redis endpoint. It does not implement Redis Cluster redirection, Sentinel discovery, Pub/Sub subscriptions, replication or `MONITOR`. Commands that suppress replies or require a different response model are rejected. RESP3 push delivery is distinct from a Pub/Sub subscription API.

Connections are not automatically reconnected and commands are never replayed. `close()` is a terminal asynchronous request; create a new client or pool to start again. See [shutdown and callback ownership](configuration.md#execution-and-shutdown) before integrating with application teardown.
