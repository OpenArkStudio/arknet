# Database clients

arknet provides native, header-only MySQL and Redis clients for independent single-node database services. Both support Boost.Asio and Standalone Asio, own their IO resources, and provide bounded connection pools. Neither requires Boost.MySQL, Boost.Redis or a database client driver.

| Capability | MySQL | Redis |
| --- | --- | --- |
| Protocol | MySQL Classic Protocol | RESP2 or RESP3; RESP3 by default |
| Operations | Text queries, typed prepared parameters/results, explicit transactions | Binary-safe commands and ordered pipelines |
| Authentication | `caching_sha2_password`, `mysql_native_password` and supported switches | Password authentication and ACL usernames |
| Transport security | Verified TLS, required by default | Plain or verified TLS; optional mTLS |
| Pools | `mysql::pool`, exclusive move-only leases | `redis::pool`, `redis::tls_pool`, exclusive move-only leases |
| Control | Deadlines, cancellation, queue and result limits | Deadlines, cancellation, queue, parser and reply limits |

## Build and run

OpenSSL is required for MySQL authentication even when a connection explicitly disables TLS. Enable `ARKNET_ENABLE_SSL` to use MySQL or secure Redis. Plain Redis does not need OpenSSL.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_EXAMPLES=ON
cmake --build build --target arknet_mysql_client arknet_mysql_pool \
  arknet_redis_client arknet_redis_pool --config Release
```

Boost is selected by default. Standalone Asio uses `ARKNET_USE_BOOST_ASIO=OFF` and, if necessary, `ARKNET_ASIO_INCLUDE_DIR`. The examples and benchmarks are independent C++ executables; running them does not require Python or Ruby.

| Guide | Complete programs | Configuration |
| --- | --- | --- |
| [MySQL](mysql/index.md) | [Queries, prepared parameters and transactions](mysql/usage.md) | [Authentication, limits and pool reset](mysql/configuration.md) |
| [Redis](redis/index.md) | [Client, pipelines and pool leases](redis/usage.md) | [RESP modes, TLS and reply handling](redis/configuration.md) |

The examples require an explicit service endpoint. Supply passwords through their documented environment variables. TLS examples keep certificate-chain and DNS/IP identity verification enabled; private CAs require the appropriate trust file.

## Connections and pool leases

An independent MySQL connection or Redis client owns one IO context and thread. A database pool shares library-owned IO resources among its connections; its connection limit is not a thread count. MySQL pools expose `io_threads`; Redis pools use one IO context and thread.

`async_acquire` returns an exclusive lease. Keep the same lease for the complete MySQL transaction or Redis `WATCH`/`MULTI`/`EXEC` sequence. Submit operations through the lease, then call `release()` or let it leave scope. Use `discard()` when reuse must be forbidden.

Returning a lease starts asynchronous processing. The pool waits for submitted callbacks and pending replies before resetting the session. Another borrower cannot use that connection during return processing.

| Return behavior | MySQL | Redis |
| --- | --- | --- |
| Reset | `COM_RESET_CONNECTION` | `RESET`, followed by configured protocol/authentication/database setup |
| Database restoration | Quoted `USE` restores the configured database | The configured database is selected again |
| Empty configured database | Returned connections are discarded; reset cannot restore an unselected database after a borrower selects one | The configured startup database is restored |
| Reset failure or expiry | Discard the connection | Discard the connection |

MySQL prepared statements belong to one connection generation and session. Reset, reconnect or return invalidates them; a handle cannot be used by another borrower. Bare-connection transaction helpers do not prevent another producer from submitting an intervening command, so use a lease or enforce ownership for the entire transaction.

## Cancellation and uncertain outcomes

Cancellation cannot retract a command that has already been sent. Neither client automatically reconnects or replays failed commands.

| Situation | MySQL | Redis |
| --- | --- | --- |
| Queued, unsent cancellation | Remove that request without consuming another response | No unsent-removal guarantee; transport-accepted writes may still execute |
| Sent cancellation or timeout | Close the session; report an unknown outcome when execution may have occurred | Complete the caller, retain ordered response slots and admission budget while replies drain; close on drain expiry |
| Diagnostic flag | `connection_result.outcome_unknown` | `result.command_may_have_run` |

A failed MySQL commit may already have committed. A failed Redis pipeline may contain replies received before failure and may have executed later sent commands. Decide retries from application semantics and durable operation identifiers.

Redis server errors are typed `error` or `bulk_error` replies; `bool(result)` checks the client status, so inspect individual replies too. RESP3 pushes use a separate handler and never consume ordinary response slots. MySQL preserves available server error code, SQLSTATE and message; a complete normal ERR response differs from a broken protocol boundary.

## Limits and callback ownership

Both clients reserve bounded work before posting it to their IO lane. Configure command count and byte limits, result/parser limits, pool connection/waiter limits and deadlines for the intended workload. These limits do not cap caller-owned storage or total process RSS.

Timeouts must be positive and representable by the monotonic clock. A zero Redis command-timeout override or either pool's acquisition-timeout override selects its configured default. MySQL `query_options.timeout` must be positive. Negative or overflowing overrides are rejected. Queue wait is included in operation deadlines.

Results own their values. MySQL buffers rows within its configured result limits; it does not provide row streaming. Redis pipelines buffer their owned replies within the pipeline limit. Check field/value types before accessing them.

Callbacks for accepted operations run on the associated library IO lane and must not block or throw. An empty or moved-from lease has no executor and rejects inline with request ID 0. Once `close_and_wait()` closes permanent admission, requests on live clients, pools and retained leases also invoke their rejection handlers synchronously on the caller. These later rejection handlers may propagate exceptions and are outside the earlier accepted-operation set. Capture application state with sufficient ownership through completion.

`close()` asynchronously requests closure; its return is not a callback-drain barrier.
Redis clients and both pools close terminally. A standalone MySQL connection can
explicitly reconnect after closing its session. See [process shutdown](../concepts/lifecycle.md#process-shutdown)
for owned runtimes and global owners.

## Supported scope and TODOs

| Area | Scope |
| --- | --- |
| MySQL | One Classic Protocol result per command; no multi-statement/multiple-result execution, `LOCAL INFILE`, compression, cursors, query attributes or arbitrary authentication plugins |
| MySQL password exchange | No plaintext full-password fallback, RSA key exchange, `mysql_clear_password`, `sha256_password` or MFA; unsupported exchanges fail explicitly |
| Redis | One configured endpoint; commands that suppress replies or require a different response model are rejected |
| Redis TODO | Cluster redirection, Sentinel discovery, Pub/Sub subscriptions, replication and `MONITOR` |
| arknet TODO | Dedicated native database coroutine APIs; workload-specific deployment acceptance |

HTTP/2, HTTP/3, named pipes, Unix domain sockets and RPC remain separate items on the [roadmap](../project/roadmap.md). RESP3 push delivery does not constitute a Pub/Sub subscription API.

## Validation and measurement

Independent-server checks include MySQL Community Server 8.4.11 and Redis 7.4.2. Client and pool suites cover successful operations, authentication/TLS rejection, malformed or partial responses, queue saturation, cancellation, session reset, lease isolation and shutdown. Protocol-codec and IO-runtime tests provide separate coverage.

| Client and pool suites | Cases | Assertions |
| --- | ---: | ---: |
| MySQL | 51 | 1,899 |
| Redis | 35 | 316 |

Deadline tests include nonpositive durations, integer extremes and clock-addition boundaries. Correctness and sanitizer checks are separate from local Release measurements; test counts are not throughput or capacity figures.

MySQL retains its runtime while asynchronous posting returns, including when a
completion destroys the connection. Separate invalid-connect and rejected-command
destruction cases cover these immediate completion paths. MySQL connection/pool
suites pass ThreadSanitizer with both Asio dependencies and AddressSanitizer+UndefinedBehaviorSanitizer with Boost.Asio;
the [testing record](../project/testing.md) identifies their scope and counts.

The [MySQL](mysql/performance.md) and [Redis](redis/performance.md) performance pages describe their compilable benchmarks, workload dimensions and reports. Record the machine/server configuration, payload, queue/pipeline depth, connections, repeated rounds, operations/s, RT percentiles and CPU percentage. Capacity requires [deployment acceptance](../project/production-readiness.md).

arknet owns protocol IO, authentication, reply correlation, pools and shutdown. ark database plugins own framework configuration and module integration; applications own transaction boundaries, business results and retry decisions. Client release does not establish ark plugin compatibility without application regression tests.

The ark framework already exposes public [MySQL](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/mysql.md) and
[Redis](https://github.com/OpenArkStudio/ark/blob/cbb659717e7d145cd1b48975a9b3ef9e6913b849/docs/reference/redis.md) coroutine modules with owner-thread delivery
on its manager shared executor. They belong to ark and do not change the native
arknet callback, IO-ownership or shutdown contracts. See the [framework integration
boundary](../project/production-readiness.md#framework-coroutine-and-database-boundary).
