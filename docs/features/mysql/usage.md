# MySQL Queries, Prepared Statements and Transactions

The complete [C++ example](../../../examples/mysql_client.cpp) connects only to the host and port supplied by the caller. It verifies a text query, executes binary-safe prepared parameters inside an explicit transaction, commits, and closes the statement. It uses literal `SELECT` expressions, so it does not create tables or change persistent data.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_EXAMPLES=ON \
  -DARKNET_USE_BOOST_ASIO=ON
cmake --build build --target arknet_mysql_client arknet_mysql_pool --config Release

export ARKNET_MYSQL_USERNAME=application_user
export ARKNET_MYSQL_CA_FILE=/path/to/ca.pem
# Supply ARKNET_MYSQL_PASSWORD through the process environment.
build/examples/arknet_mysql_client mysql.example.com 3306
build/examples/arknet_mysql_pool mysql.example.com 3306
```

With a multi-configuration generator, use `examples/Release/`. For Standalone Asio, configure `ARKNET_USE_BOOST_ASIO=OFF` and set `ARKNET_ASIO_INCLUDE_DIR` when needed. No scripting runtime is required to run the executable.

The example accepts `ARKNET_MYSQL_PASSWORD`, optional `ARKNET_MYSQL_DATABASE`, `ARKNET_MYSQL_CA_FILE` and `ARKNET_MYSQL_SERVER_NAME`. It neither embeds a password nor accepts one on the command line. TLS is required; an unset CA file uses the default trust configuration. A server with a private CA needs the correct CA file. The example does not disable certificate checks to connect.

## Queries and results

Include `<arknet/mysql/connection.hpp>`. After `async_connect` succeeds, call `async_query(sql, handler, options)` for a single query. A `query_result` contains `status`, owned `columns`, typed `rows`, affected-row and insert-ID metadata, status flags and warnings.

```cpp
client.async_query("SELECT 42 AS number", [](arknet::mysql::query_result result) {
    if (!result)
    {
        // Inspect result.status.code and outcome_unknown before deciding what to do next.
        return;
    }
    // Inspect the field_value alternative before accessing its value.
});
```

SQL is passed as SQL, not escaped or rewritten. Use prepared parameters for user-provided values rather than concatenating them into query text. Results are accumulated in memory within configured row, column and byte limits; this API is not a row-streaming interface.

## Prepared parameters

`async_prepare` returns a `prepare_result` with a `statement`. Pass the handle and a vector of `field_value` values to `async_execute`. Parameters are encoded in the binary protocol without SQL interpolation. For example, the complete program prepares `SELECT ? AS number, ? AS payload` and supplies `std::int64_t(42)` plus `std::string("ark\0net", 7)`.

Check `statement.parameter_count()` and result status. A statement belongs to its original authenticated session; using it on another connection, after reset, or after reconnect reports `invalid_statement`. Handle copies share ownership. Explicit `async_close(statement, handler)` closes it; otherwise the last handle schedules cleanup. Closing the connection also releases server-side statement resources.

## Transactions and callback lifetime

Call `async_begin`, submit the transaction's commands in order, then call `async_commit` or `async_rollback`. A bare connection does not prevent another producer from inserting commands into that sequence; applications must control ownership and ordering for the entire transaction.

The complete program waits on promises from the main thread. Each callback owns a shared promise so that a timeout or early return cannot leave it referring to a destroyed stack object. Callbacks run on the library's IO thread and must not throw or block. In particular, do not wait on these futures inside a callback.

A failed commit with `status.outcome_unknown == true` may already have committed on the server. Do not retry the transaction automatically. See [cancellation and outcomes](configuration.md#cancellation-and-unknown-outcomes).

## Permanent shutdown of an independent connection

`close()` cancels the current session and permits a later `async_connect`.
For an independently constructed `connection`, call `close_and_wait()` from a
non-IO owner thread when the object will no longer be used:

```cpp
// First stop application producers. Do not hold locks required by callbacks.
if (!client.close_and_wait())
{
    // Called on its own IO worker: get_last_error() is operation_not_supported.
}
```

Success means accepted operations have completed once, their library-held handler
captures have been destroyed, retained statement cleanup has been disabled, and
this connection's IO worker has exited and been joined. Concurrent waiters observe
the same completed shutdown. Ordinary `close()` retains its existing semantics.

The permanent submission gate closes before waiting. Later `async_*` calls reject
synchronously on their caller with `not_connected` and return request ID 0;
`async_connect` also completes synchronously. These rejection handlers may propagate
exceptions. `cancel()` and `close()` then do nothing. Before termination, accepted
callbacks continue to run on the IO lane and must not throw. Allocation or system
exceptions from shutdown propagate; they do not indicate a completed barrier.

Calling from its own IO worker, a pool-owned child, or a reentrant publication or
finalization returns false with `operation_not_supported` without starting permanent
shutdown. Never wait from another library IO worker either: that restriction is a
calling contract, not a global worker check. A handler that never returns can prevent
shutdown from completing; there is no timeout that implies safe teardown.

This operation does not stop another connection or a pool. It is not a process
cleanup-worker or dynamic-library unload barrier. The host must still stop and wait
for application callers and destroy their objects before unloading code.

## Connection pool

The complete [pool example](../../../examples/mysql_pool.cpp) acquires an exclusive connection lease, starts a transaction, verifies a query and commits before returning the lease. It uses the same endpoint, credential and TLS environment variables as the client example.

Include `<arknet/mysql/pool.hpp>`, set `pool_options.connection`, and call `async_acquire`. A successful `pool::acquire_result` contains a move-only `connection` lease. Submit query, prepare, execute and transaction operations through the lease; it does not expose the raw reusable connection. Keep the same lease across the complete transaction.

`release()` or lease destruction starts return processing. Pending command callbacks must finish before the connection is reset and restored to the configured database. `discard()` forbids reuse. A returned lease cannot submit useful work, and prepared handles cannot escape to another borrower. An empty configured database causes returned connections to be discarded because reset cannot clear a database selected by a previous borrower. Set `pool_options.connection.database` when reusable pooled sessions are required.

See [pool limits and reset](configuration.md#pool-limits-and-reset) for deadlines, database isolation and IO ownership.


### Permanent pool shutdown

Ordinary `pool::close()` requests cancellation asynchronously. For the pool-owned
IO boundary, stop application producers and call `pool::close_and_wait()` from a
non-library-IO owner thread:

```cpp
if (!connections.close_and_wait())
{
    // Own IO worker or reentrant call: operation_not_supported.
}
```

Success means accepted acquisitions and lease operations have delivered their
terminal callbacks, callback captures have retired, and all pool-owned IO workers
have exited and been joined. Children removed by an earlier discard are included.
Retained leases and prepared statements are made inert: they retain no child
executor, timer, SSL stream or automatic-cleanup callable. An individual lease
never stops the shared runtime; discarding it leaves other leases usable.

The permanent gate covers acquisition, cancellation, lease submission and return.
Later `async_acquire` and lease `async_*` calls reject synchronously on their caller
with `not_connected` and request ID 0. Rejection handlers may throw to the caller.
Later cancellation and return calls do not post work. Concurrent waiters observe
the same completed shutdown; shutdown exceptions propagate and do not indicate a
completed boundary. Accepted IO callbacks must return without throwing.

Waiting on a pool worker or reentrantly during publication/finalization returns
false with `operation_not_supported`. Do not wait on another library IO worker,
hold a lock needed by a callback, or wait for a callback that itself needs the
waiting owner thread. There is no timeout that implies successful teardown.
Application callers and their objects must still be stopped and destroyed before
unloading code; this is not a process cleanup-worker or dynamic-library unload
barrier.
