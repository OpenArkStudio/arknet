# MySQL

arknet provides a native, header-only client for a single MySQL Classic Protocol endpoint. It implements protocol framing, authentication, queries and prepared commands using arknet's IO runtime. It does not require Boost.MySQL or a MySQL client driver.

| Area | API and behavior |
| --- | --- |
| Connection | `mysql::connection`, domain or IP endpoint, configurable database and credentials |
| TLS | Required by default, certificate-chain and DNS/IP identity verification |
| Authentication | `caching_sha2_password` and `mysql_native_password`, supported authentication switches |
| Queries | FIFO text queries with owned column metadata and typed rows |
| Prepared statements | Typed binary parameters and results, session-bound statement handles |
| Transactions | Explicit begin, commit and rollback on one connection |
| Connection pool | Bounded `mysql::pool`, exclusive move-only leases and reset before reuse |
| Control | Queue/result limits, deadlines, cancellation and session reset |
| Failure handling | Server code and SQLSTATE, explicit unknown outcomes, no command replay |

## Get started

- [Query, prepared statement and transaction example](usage.md).
- [Configuration and limits](configuration.md).
- [Performance methodology](performance.md).

Enable `ARKNET_ENABLE_SSL` to use the connection API. OpenSSL provides TLS and authentication hashing even when a connection explicitly disables TLS. Both Boost.Asio and Standalone Asio are supported; the library owns IO contexts and threads.

## Scope

This client implements one Classic Protocol result per command. It does not negotiate multi-statement or multiple-result execution, `LOCAL INFILE`, compression, cursor fetching, query attributes, optional result metadata or arbitrary authentication plugins. An unsupported protocol exchange fails explicitly instead of attempting to continue on an ambiguous response boundary.

Real-server validation includes MySQL Community Server 8.4.11. Protocol tests cover fragmented packets, typed values, limits and cancellation; deployment capacity and performance require separate local measurements. [Performance](performance.md) describes that boundary.
