# MySQL Configuration and Limits

## Connection and TLS

`connect_options` configures `host`, a numeric `port` (default 3306), `username`, `password`, optional `database`, and a 10-second `timeout`. The deadline spans DNS resolution, connection, TLS negotiation and MySQL authentication.

`tls_mode::required` is the default. The client sends the MySQL SSLRequest before starting TLS, verifies certificate trust and DNS/IP identity, and fails when TLS is unavailable. `ca_file` selects a private trust file; otherwise default trust paths are used. `server_name` defaults to `host`: overriding it changes the verified identity and DNS SNI, not the network destination or the requirement to verify the peer.

`tls_mode::disabled` is an explicit plaintext option. It does not enable plaintext full-password authentication; a `caching_sha2_password` full-auth request without verified TLS fails. OpenSSL is still a build dependency for authentication hashes. There is no automatic TLS downgrade or RSA public-key password exchange.

Supported password methods are `caching_sha2_password` and `mysql_native_password`, including supported server-requested switches. Unknown plugins and unsupported authentication exchanges fail explicitly. `mysql_clear_password`, `sha256_password`, MFA and arbitrary plugin loading are not provided.

## Query and admission limits

| Option | Default | Scope |
| --- | --- | --- |
| `connect_options.max_message_size` | 64 MiB | One logical Classic Protocol message |
| `connect_options.max_pending_commands` | 64 | Additional pending requests beyond the one active request |
| `connect_options.max_pending_bytes` | 64 MiB | Retained request and parameter storage across active, queued and not-yet-posted work |
| `query_options.timeout` | 10 s | Submission through result completion, including queue time |
| `query_options.max_rows` | 100000 | Result rows retained by one command |
| `query_options.max_columns` | 4096 | Result columns |
| `query_options.max_result_bytes` | 64 MiB | Accumulated result processing budget |

Admission reserves count and retained input capacity before posting command work to the IO thread. Rejected requests release their payload storage and complete with `queue_full`; a blocked IO thread cannot admit an unlimited backlog outside these counters. These limits do not cap caller-owned data or total process RSS.

Requests are FIFO, with one command active on the wire. `query_options` is accepted by query, prepare, execute, statement-close and transaction helpers. Results are fully buffered. A protocol error, unsupported continuation, result overflow or incomplete response cannot leave a reusable connection at an uncertain packet boundary; the connection closes.

## Typed fields

`field_value` owns one of: `std::monostate` for SQL NULL, signed or unsigned 64-bit integer, `double`, `std::string`, `date_value`, `datetime_value`, or `time_value`. Text query rows and prepared binary rows are decoded using column metadata. DECIMAL, BIT, JSON and byte/string fields retain their bytes instead of losing precision or applying an implicit character-set conversion. Temporal values retain server components without automatic timezone conversion.

Prepared parameters use these same types and binary encoding, not SQL escaping. Unsupported field encodings and invalid ranges fail explicitly. Validate the active variant alternative before reading it.

## Cancellation and unknown outcomes

Each command returns a `request_id` for `cancel(id)`. Cancelling or expiring a queued command removes that request without disturbing the active command's reply. Cancelling or timing out an active sent command closes the session so its remaining response cannot be mistaken for a later command.

`connection_result.outcome_unknown` is true when a failed command may already have executed. This includes transport loss while waiting for `COMMIT`: the transaction may have committed even when the client received no success reply. Pending unsent commands fail without being replayed. Applications must decide recovery using operation semantics and durable application identifiers; the client never retries writes implicitly.

`code` distinguishes local, protocol, transport and server failures; `transport_error`, `server_code`, `sql_state` and `message` retain available diagnostics. A complete normal server ERR reply can leave the connection reusable, while malformed or partial responses close it. Do not equate every server error with an unknown write outcome.

## Pool limits and reset

`pool_options` provides `max_connections` (8), `max_waiters` (1024), `io_threads` (1), `acquire_timeout` (5 seconds), `reset_timeout` (5 seconds), and the configured `connection`. IO contexts and threads belong to the library; connections share the pool runtime. Increasing the connection limit does not implicitly create one thread per connection.

`async_acquire` returns an acquisition ID for `pool.cancel(id)` and accepts an optional timeout override. A lease exclusively owns one connection until returned; its command IDs are cancelled through `lease.cancel(id)`. Acquisition limits include waiting and connecting requests. No command or transaction is replayed when a connection is replaced.

The return deadline begins when `release()` or lease destruction requests return. It includes pending callback drain, COM_RESET_CONNECTION, and database restoration. MySQL reset does not restore the selected database; the pool issues a quoted `USE` for its configured database before reuse. If the configured database is empty, every returned connection is discarded, because reset cannot restore an unselected database after a borrower selects one. Failed reset, failed restoration, deadline expiry or unhealthy connections also cause disposal.

Lease destruction and `release()` initiate return without blocking the caller; reuse waits until all submitted callbacks have returned. Prepared statements from that session become invalid after reset or disposal and cannot cross borrowers. Empty or moved-from leases have no executor and reject inline with request ID 0. While permanent pool admission remains open, live released-lease rejection completes on the owned IO thread. Once permanent admission closes, retained leases reject synchronously on their caller with request ID 0. `close()` terminates the pool asynchronously and is not a callback-drain barrier.

## Statements, reset and lifetime

`prepared_statement` is an opaque, copyable shared handle bound to one connection generation and session. Copies share close state. `async_close` sends a no-response COM_STMT_CLOSE; successful completion is a client-side write boundary, not a server acknowledgement. Last-handle destruction schedules the same resource cleanup when the session remains valid.

`async_reset` performs COM_RESET_CONNECTION. Success invalidates existing statement handles and resets server-side session and transaction state. Do not retain a statement across reset or reconnect, or use it on another connection. A bare connection's transaction helpers do not add exclusive transaction ownership.

The library owns one IO context/thread per independent connection. Callbacks for accepted operations run exactly once on its IO thread and must not throw or block. Empty-lease rejections and requests rejected after permanent admission closes invoke their handlers synchronously on the caller; these rejection handlers may propagate exceptions. `close()` requests cancellation asynchronously; its return is not a callback-drain barrier. Application objects captured by callbacks must remain alive through completion. Destruction from a callback is supported without requiring the caller to manage IO threads. There is no automatic reconnect or command replay.
