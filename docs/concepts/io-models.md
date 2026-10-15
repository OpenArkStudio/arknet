# IO Models and Coroutines

## Execution Models

Asio permits one or several threads to call `io_context::run()`. A single runner
implicitly serializes its handlers. Several runners can execute different
handlers concurrently; a strand serializes the handlers associated with it.
Strands do not pin a connection to an operating-system thread.

| Contexts and threads | Suitable workloads | Tradeoff |
| --- | --- | --- |
| One context/one thread | Modest IO traffic, small services, ordered state machines | One long handler delays every connection |
| One context/several threads with independent strands | Many connections with uneven activity | Scheduling one context from several threads adds contention; callbacks can change threads |
| Several contexts/one thread each | Stable partitions, predictable connection loads | A busy partition cannot borrow another partition's thread |

The owned `arknet::io_pool` uses one context and one worker per lane. Its first
lane handles the server listener; sessions use the remaining lanes when present.
`arknet::io_pool` is the public scheduler name; `arknet::iopool` remains an alias.
External contexts may use one or multiple runners. arknet creates a strand for
each supplied external lane and never stops the host context.
Directly constructed `arknet::io_t` lanes use a strand by default. The explicit
`serialize = false` setting requires a single context runner, as used internally
by the owned pool.

Passing one context directly to a server creates one lane: its sessions share
that lane's strand. Supply several lanes to allow concurrent session handlers,
even when all lanes refer to the same context:

```cpp
asio::io_context context;
auto work = asio::make_work_guard(context);
std::vector<asio::io_context*> lanes(5, &context);
arknet::tcp_server server(1024, 16 * 1024 * 1024, lanes);
// Run context.run() on the desired number of host threads.
```

Every client passed the same external context gets its own lane. UDP server
sessions share the datagram socket and its serialized lane. Additional runner
threads therefore do not make one UDP server's receive handlers concurrent.
User state accessed by independent lanes needs synchronization. Avoid relying
on thread-local state across asynchronous operations.

Keep the contexts, runners and work guard alive until all network objects stop.
Stop objects first, release the work guard, then join the host runners. A network
callback should request shutdown and let an owner thread wait for completion.
Let every runner drain before destroying any context: a queued cancellation
handler in one context may retain a session or strand from another context.
See [lifetime and send rules](sending.md).

An owned endpoint's final reference can be released on an IO callback after its
close completes. arknet transfers pool draining and joining to an internal cleanup
thread, avoiding a worker waiting for itself. Destruction of an already closed
endpoint does not enqueue another stop operation. Owned endpoints in the same
executable or shared library share the cleanup thread; it does not execute
network business callbacks. Callers still own the lifetime of external contexts.

## Coroutine Workloads

A coroutine changes how asynchronous control flow is expressed; it does not
create a thread or distribute CPU work. `co_await` releases the runner while an
operation is pending, but computation between suspension points occupies that
runner. A suspended coroutine can resume on a different thread running the
same context. Spawn it on a strand when other operations access the same connection
state; a sequential coroutine alone does not serialize those other operations.

| Business workload | Starting choice | Reason |
| --- | --- | --- |
| IO-bound gateway, chat or HTTP clients | One context/several threads with per-connection strands | Independent connections can use available threads as activity changes |
| Small agent or control-plane service | One context/one thread | Serial state handling with low scheduling overhead |
| Partitioned game sessions or tenant workers | One context and thread per partition | Explicit ownership and stable assignment of application state |
| Compression, parsing or expensive request computation | Either IO model plus a bounded CPU executor | Long CPU tasks otherwise delay unrelated IO; awaiting their results keeps IO runners available |

These are starting choices, not universal rankings. One busy connection remains
serialized by its strand in either model. More IO runners cannot parallelize its
state machine. Measure the actual connection distribution and handler cost.
The first release provides callback-based arknet endpoints; the coroutine
benchmark uses native Asio `awaitable`/`co_spawn` to test scheduler choices.
A public coroutine endpoint API is deferred.

## Caller-driven execution

`arknet::manual_runtime` owns a context and work guard without starting a worker
to run user handlers. Asio platform services may still use internal helper
threads, such as Windows timers or DNS resolution.
Call `poll(max_handlers)` from the constructing thread, for example during an
application's update loop. Other threads can use its standard Asio executor to
post completions:

```cpp
#include <arknet/base/manual_runtime.hpp>

arknet::manual_runtime runtime;
asio::post(runtime.get_executor(), [] { /* Runs in the thread calling poll. */ });
runtime.poll(64); // At most 64 ready Asio handlers; never waits for work.
```

Bind `co_spawn` and timers to `runtime.get_executor()` to keep coroutine execution
on that same thread. Continue polling while operations are pending. Idle polls
return zero; later posts still run without restarting the runtime.

`poll(0)` executes nothing. A foreign-thread or reentrant poll throws
`std::logic_error`; handler exceptions propagate, and a later poll remains valid.
The budget counts Asio handlers, not elapsed time or inline calls inside a
handler. Posting is not bounded by this dispatch budget.

The executor does not keep the context alive. Finish or cancel external
operations and stop their producers before destroying the runtime on its owner
thread. Active polling pins the context until that call returns; releasing the
runtime does not cancel its handlers. Destruction releases unexecuted captures
and is **not** an IO drain, join or dynamic-library unload barrier. Use the
runtime's poll entry point rather than running or stopping its underlying context.
Native endpoint callbacks still use their own executors unless explicitly bridged
to this executor; the runtime alone does not change their callback thread.

## Asio References

- [Threads and Asio](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/threads.html)
- [Strands](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/strands.html)
- [C++20 Coroutines](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/composition/cpp20_coroutines.html)


See the [performance comparison](../performance/io-comparison.md) for throughput, RTT and CPU results.
