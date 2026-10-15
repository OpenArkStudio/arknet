# Lifecycle and Execution

## Lifecycle and Execution

Configure listeners, protocol options and TLS identity before starting an
object. Keep the object and every resource captured by its callbacks alive
until shutdown completes. A receive callback's data view is valid only for the
duration of that callback; copy it to retain it.

`is_started()` and `is_stopped()` read atomic lifecycle state and can be queried
from another thread while the object is alive. Each result is a snapshot; it
does not reserve the connection or make direct socket access safe across threads.

Server `get_listen_address()` and `get_listen_port()` read synchronized endpoint
snapshots during start and close. Direct `acceptor()` access still follows Asio's threading rules.

Owned IO pools use one context and one worker per lane. External contexts can
have one or multiple threads calling `run()`. Each external lane uses a strand
that serializes its callbacks. A callback can run on a different host thread
on its next invocation. Independent lanes can execute concurrently, so shared
application state still needs synchronization.

For transport clients and servers:

| Operation | Contract |
| --- | --- |
| `stop()` from an owner thread | Initiate shutdown and wait for completion |
| `request_stop()` | Schedule shutdown; suitable inside a network callback |
| `wait_stopped()` from an owner thread | Complete shutdown and report whether waiting was allowed |
| `wait_stopped()` from a worker of the involved context/pool | Return `false` with `operation_not_supported` |

For TCP-based endpoints, owner-thread completion includes canceled native I/O,
accepted callback captures and session work that still borrows the server.
Stopping rejects new user timers, delayed posts and condition waits; an explicit
restart reopens them. Already accepted condition callbacks still run during
shutdown. Stop application producers before shutdown: immediate posts remain
available, so callbacks must not submit work indefinitely. Serialize lifecycle
operations such as stop followed by restart. TCP servers, including their HTTP,
WebSocket and TLS variants, allow concurrent owner stop waiters. If a callback's
capture or a session destructor reenters stop on the same owner thread during
drain, `stop()` returns without waiting and sets `operation_not_supported`;
`wait_stopped()` returns `false` with that error.

Database clients use their own asynchronous `close()` contract; see
[database shutdown](../features/database-clients.md#limits-and-callback-ownership).

Keep external contexts, their runners and work guards running throughout start,
send and shutdown. arknet does not stop the host context. Stop and destroy the
network objects first, then release the host work guards and join the runners.
Let `run()` drain queued completion handlers before it returns. `io_context::stop()`
stops dispatching handlers; it does not perform this drain. Keep every context
alive until its runners finish, including handlers retaining sessions or strands
from another context.
Do not destroy an owned IO pool on one of its workers; its destructor terminates
in that case.

The standalone `arknet::timer` starts during construction and has `stop()`, but
no `request_stop()` or `wait_stopped()`. Calling its `stop()` in a callback is
nonblocking. An owner thread can call `stop()` again to drain pending completion
handlers while the external context is still running.

## Process shutdown

Stop network work and release its owners before returning from `main`. When a
callback releases the last endpoint owner on an owned IO worker, arknet defers
pool deletion to a cleanup worker. Process cleanup waits for those owned pools,
their callbacks and TLS resources before OpenSSL's normal global cleanup. The
cleanup worker starts when the first owned IO pool is constructed, rather than
when a header is included.

For globals, include an arknet IO header before declaring the owning object in
each translation unit. This also covers an initially empty `shared_ptr` assigned
from `main`. A forward-declared or type-erased global owner in another translation
unit without that include must be explicitly reset before `main` returns; its
static destruction order cannot be inferred by arknet.

The default Standalone Asio configuration uses native thread storage. If Asio was
included first, or the application explicitly selects legacy pthread-key thread
storage, keep that configuration consistent across translation units and destroy
all owned endpoints and pools before leaving `main`. External IO runners remain
the application's responsibility.
