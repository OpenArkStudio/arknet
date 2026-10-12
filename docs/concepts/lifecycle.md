# Lifecycle and Execution

## Lifecycle and Execution

Configure listeners, protocol options and TLS identity before starting an
object. Keep the object and every resource captured by its callbacks alive
until shutdown completes. A receive callback's data view is valid only for the
duration of that callback; copy it to retain it.

Owned IO pools use one context and one worker per lane. External contexts can
have one or multiple threads calling `run()`. Each external lane uses a strand
that serializes its callbacks. A callback can run on a different host thread
on its next invocation. Independent lanes can execute concurrently, so shared
application state still needs synchronization.

For clients and servers:

| Operation | Contract |
| --- | --- |
| `stop()` from an owner thread | Initiate shutdown and wait for completion |
| `request_stop()` | Schedule shutdown; suitable inside a network callback |
| `wait_stopped()` from an owner thread | Complete shutdown and report whether waiting was allowed |
| `wait_stopped()` from a worker of the involved context/pool | Return `false` with `operation_not_supported` |

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
