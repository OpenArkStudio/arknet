# Sending, Ownership and Backpressure

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


Kernel socket buffers are separate from application queues; see [Buffer configuration](../guides/socket-buffers.md).
