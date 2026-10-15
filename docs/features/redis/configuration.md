# Redis Configuration and Limits

## Connection and protocol

Configure `redis::connect_options` before `async_connect`: `host`, `port`, `username`, `password`, `client_name`, `database` and `protocol`. Domain names are resolved by the networking library. The Redis port is explicit; HTTP/HTTPS default-port rules do not apply.

`protocol_version::resp3` is the default and requires a successful `HELLO 3` reply. There is no silent fallback or command replay when negotiation fails. Select `protocol_version::resp2` explicitly for RESP2 authentication and startup. An empty username with a supplied password authenticates the default user; an explicit username also permits an empty password when allowed by its ACL. Do not change protocol mode by sending raw `HELLO` while other requests are pending.

## Commands and typed replies

`command` is `std::vector<std::string>`. Each element is one binary-safe argument. `async_pipeline` preserves command order and completes once with a `result` whose `replies` own their data. RESP3 pushes use `set_push_handler` and do not consume pipeline reply slots; pushes without a handler are ignored.

| `value_type` | Data |
| --- | --- |
| `simple_string`, `bulk_string`, `error`, `bulk_error`, `big_number`, `verbatim_string` | `text` |
| `integer`, `real`, `boolean` | Corresponding named member |
| `array`, `set`, `push` | `elements` |
| `map`, `attribute` | Alternating key/value entries in `elements`, retaining all key types |
| `null` | No payload |

`attributes` contains RESP3 attributes attached to the value. Inspect types before accessing data. A server error is a typed reply, not a transport failure: `bool(result)` checks `client_errc`, so applications must also inspect individual replies. Failed pipelines can contain replies received before the failure.

## Limits and deadlines

Set `redis::client_options` when constructing the client. A pool uses `pool_options.client_configuration`.

| Option | Default | Scope |
| --- | --- | --- |
| `max_pending_commands` | 1024 | Admitted commands, including sent requests awaiting drain |
| `max_pending_bytes` | 16 MiB | Admitted encoded command bytes |
| `max_pipeline_response_bytes` | 64 MiB | Reply values retained for one pipeline, including value storage |
| `connect_timeout` | 5 s | Submission, resolution, transport connection and Redis handshake |
| `command_timeout` | 5 s | Command submission through complete reply delivery |
| `drain_timeout` | 5 s | Remaining replies after sent-request cancellation or timeout |

`response_limits` additionally bounds buffered input, one RESP message, bulk values, lines, nested value counts and depth. Defaults are 64 MiB for buffer/message/bulk bytes, 64 KiB per line, 1,048,576 values and depth 64. Limits and deadlines must be positive; configured depth cannot exceed 128. These are protocol and admission limits, not a process RSS limit or a replacement for application-level backpressure.

An optional positive timeout argument on `async_execute` or `async_pipeline` overrides `command_timeout`. The returned `request_id` can be passed to `cancel`. A full admission queue reports `client_errc::queue_full` through the completion callback; it does not silently discard commands.

### Unknown write outcomes

Cancellation completes the caller; it cannot retract a write already accepted by the transport. If a failed result has `command_may_have_run == true`, the server may have executed any sent commands even though their replies were not delivered. Do not automatically retry non-idempotent writes.

Cancelled or timed-out sent requests keep their ordered response slots and admission budget until replies drain. This prevents an old reply from being assigned to a newer command. If draining exceeds `drain_timeout`, the connection closes and outstanding requests fail. No failed command is automatically replayed.

## Pool leases and state isolation

`pool_options` defaults to 8 connections, 1024 pending acquisitions, a 5-second acquisition timeout and a 5-second reset timeout. `max_waiters` includes acquisitions waiting for a new connection to finish connecting. `async_acquire` accepts a timeout override and returns an ID for `pool.cancel(id)`. A lease exposes command submission and cancellation, without exposing a raw reusable client.

Keep a lease across a complete transaction or other connection-local operation. Returning it waits for submitted callbacks and ordered replies, then runs [`RESET`](https://redis.io/docs/latest/commands/reset/) and repeats the configured protocol/authentication/database setup. This isolates unfinished transactions, watched keys, selected databases and authentication changes. The reset deadline includes waiting for earlier replies. Reset failure, lack of server support or an unhealthy connection causes disposal; commands are not replayed on its replacement.

Calling `release()` twice is harmless. Submissions on a released lease fail with `closed`; an empty or moved-from lease has no executor and rejects synchronously with request ID 0. `discard()` returns the lease without permitting reuse. Pool connections share library-owned IO resources; `max_connections` is not a thread count.

## TLS options

Use `redis::tls_client` or `redis::tls_pool` with `ARKNET_ENABLE_SSL=ON`:

```cpp
arknet::redis::client_options options;
options.tls_certificate_authority = "/path/to/ca.pem";
options.tls_certificate_file = "/path/to/client.pem";
options.tls_private_key_file = "/path/to/client-key.pem";
arknet::redis::tls_client client(options);
```

The client certificate and private key are optional for one-way TLS, required when the server demands mTLS. `tls_private_key_password` supports encrypted keys. A pool applies these fields through `client_configuration`; `pool_options.certificate_authority`, when provided, selects its CA file. Certificate-chain and DNS/IP identity verification remain enabled. The connection host controls identity verification; a Redis client name does not alter it.

## Execution and shutdown

The library owns IO contexts, threads and transport lifetimes. A standalone client owns one IO context/thread; a pool owns one context/thread shared by its clients. Accepted command callbacks, connection callbacks and pushes run on the associated IO lane; rejected client submissions are also completed there. Callbacks must not throw or block. The empty-lease rejection above is the synchronous exception.

`close()` atomically rejects further useful work and schedules terminal shutdown. Pending operations complete once, but `close()` returning does **not** mean their callbacks or transport cleanup have drained. Destruction may request the same deferred shutdown, including when invoked from a callback. Application state captured by callbacks needs its own lifetime ownership; a request deadline or object destruction does not make a dangling reference safe.

There is no automatic reconnect. Construct a new client after terminal close. A pool's `close()` also terminates queued acquisitions and its connections; it is not a callback-drain barrier.
