# Lifecycle, Sends and Errors

These rules apply to the callback-based clients, servers and sessions. See
[Quick Start](quickstart.md) for a complete program and [IO Models](threading.md)
for scheduler choices.

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

## Sending and Ownership

The rules below describe individual client/session sends. Server broadcast
overloads forward data to sessions; use each session's completion when per-peer
outcomes matter.

`async_send()` owns accepted strings, string views, spans and Asio buffers until
completion. It also copies views returned by a data filter. The caller can
reuse the source storage after submission returns. Receive views remain
callback-local even when used as input to an echo send.

The `bool` result means that the operation was admitted, not that the peer
received or processed the message. Completion callbacks support
`(const error_code&, size_t)`, `(size_t)` or `()`, including generic, overloaded
and move-only callables.

| Submission outcome | Result | Completion execution |
| --- | --- | --- |
| Invalid input, disconnected object or queue limit | `false` | Inline on the calling thread, with an error and zero bytes |
| Admitted operation | `true` | On the object's IO executor after transport completion or cancellation |

A callback can therefore run before `async_send()` returns on rejection. Avoid
assuming every completion is posted, or holding a lock that the callback will
acquire. Prefer the two-argument callback to inspect the operation's own error.
Successful transport completion does not acknowledge peer application handling;
add an application-level response when that is required.

### Backpressure

Each object's send queue defaults to 16 MiB of payload and 1024 pending
operations. Empty payloads also count toward the operation limit.

| API | Meaning |
| --- | --- |
| `set_max_send_buffer_size(bytes)` | Set the queued-payload byte limit |
| `get_max_send_buffer_size()` | Read that limit |
| `get_queued_send_buffer_size()` | Observe currently reserved payload bytes |

An operation that exceeds either limit is rejected with `no_buffer_space`.
The byte count is an observation, not a reservation for a later send. Stop
producing, reduce application concurrency, or retry according to application
policy after completions release capacity. Raising the limit increases possible
memory use and does not increase the peer's receive rate.

### Futures and send()

The future overload returns `std::future<std::pair<error_code, size_t>>`:

```cpp
auto completion = client.async_send(std::string("hello"), asio::use_future);
// On an owner thread, while the IO context continues running:
auto [error, bytes] = completion.get();
```

Never wait for an incomplete send or posted-task future from a worker of the
same context. This also applies to a different strand on that context.

`send()` submits an asynchronous operation. Outside the involved context's
workers, it waits and returns the transport's byte count. On a worker, an
incomplete operation continues asynchronously while `send()` returns zero and
sets `in_progress`. An immediately ready failure reports its actual error.
Use `async_send()` in callbacks so completion is explicit.

## Timers, post and Reconnect

`post(callback)` queues work on the object's executor. `dispatch(callback)` can
run inline when already on that executor. The delayed `post(callback, delay)`
uses a steady timer. Future overloads return the callback's result.

```cpp
using namespace std::chrono_literals;
arknet::timer timer;
timer.post([] { /* immediate task */ });
timer.start_timer("heartbeat", 1s, [] { /* repeated task */ });
timer.start_timer("once", 100ms, 1, [] { /* one invocation */ });
// Later, on the owner thread:
timer.stop();
```

Starting a timer with an existing key replaces it. `stop_timer(key)` cancels
that key; `stop_all_timers()` cancels keyed timers. `stop_all_timed_events()`
(and its alias `stop_all_timed_tasks()`) cancels delayed posts. Cancellation is
scheduled on the executor, and the underlying wait handlers still need to
complete. By default, a canceled wait suppresses its user callback. A canceled
posted-task future whose callable is suppressed becomes ready; retrieving its
result throws `std::future_error` with `std::future_errc::broken_promise`. Defining
`ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR` enables timer callbacks on errors;
inspect `get_last_error()` in those callbacks and use the macro consistently
across translation units.

Clients enable automatic reconnect by default with a one-second retry delay.
Configure it before starting:

```cpp
client.set_auto_reconnect(true, std::chrono::seconds(2));
// Or disable automatic retry:
client.set_auto_reconnect(false);
```

A successful reconnect advances the connection generation. An old queued send
cannot be replayed on the new connection; stale operations complete with
`operation_aborted`. Re-establish application state in the connect callback
only after checking its error.

## Errors and TLS

`get_last_error()` is thread-local. Read and copy it inside the callback or
immediately after the synchronous call that reports an error. Reading it later
on another thread cannot retrieve the earlier operation's error. A send's
explicit completion error or future result is the preferred error source.

```cpp
client.bind_connect([]
{
    const arknet::error_code error = arknet::get_last_error();
    if (error) {
        // Record or pass this value to application state.
    }
});
```

TLS clients verify certificate chains and DNS/IP identity by default and set
SNI for DNS hosts. Servers need a usable certificate and matching private key.
mTLS also requires a trusted client CA and server verification mode
`verify_peer | verify_fail_if_no_peer_cert`. Explicit `verify_none` disables
verification. Repository certificates and keys are for testing only.

For a failed operation, use [Troubleshooting](troubleshooting.md). Current
validation evidence and limits are recorded in [Testing](testing.md).
