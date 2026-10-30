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

| Scheduler | Lane contract |
| --- | --- |
| Owned `arknet::io_pool` (`arknet::iopool` alias) | One context and worker per lane; first lane listens, sessions use other lanes when present |
| External contexts | One or multiple runners; arknet creates a strand per supplied lane and never stops the host context |
| Direct `arknet::io_t` | Strand by default; `serialize = false` requires one context runner, as in the owned pool |

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

<Aside type="caution" title="Keep external contexts alive">

A queued cancellation handler can retain another context's session or strand.
In callbacks, request shutdown; let an owner thread wait for completion.

</Aside>

<Steps>

<Step title="Stop network objects">

Keep contexts, runners and the work guard alive until every object stops.

</Step>

<Step title="Release the work guard and drain runners">

Join every host runner before destroying any context. See [lifetime and send rules](sending.md).

</Step>

</Steps>

| Owned endpoint cleanup | Contract |
| --- | --- |
| Final reference released in an IO callback after close | Internal cleanup thread drains and joins the pool, avoiding a self-join |
| Already closed endpoint destroyed | No additional stop operation is queued |
| Cleanup thread scope | Shared by owned endpoints in one executable or shared library; never runs network business callbacks |

External context lifetime remains the caller's responsibility.

## Coroutine Workloads

Coroutines express asynchronous flow without creating threads or distributing CPU work.

| Execution | Contract |
| --- | --- |
| Pending `co_await` | Releases the runner; computation between suspension points still occupies it |
| Resume | May use another thread running the same context |
| Shared connection state | Spawn on a strand; one sequential coroutine does not serialize other operations |

| Business workload | Starting choice | Reason |
| --- | --- | --- |
| IO-bound gateway, chat or HTTP clients | One context/several threads with per-connection strands | Independent connections can use available threads as activity changes |
| Small agent or control-plane service | One context/one thread | Serial state handling with low scheduling overhead |
| Partitioned game sessions or tenant workers | One context and thread per partition | Explicit ownership and stable assignment of application state |
| Compression, parsing or expensive request computation | Either IO model plus a bounded CPU executor | Long CPU tasks otherwise delay unrelated IO; awaiting their results keeps IO runners available |

Measure connection distribution and handler cost: these are starting choices, not rankings.
A busy connection's strand serializes its state machine in either model; extra runners cannot parallelize it.

| Interface | Availability |
| --- | --- |
| arknet endpoints and native database clients | Callback APIs; dedicated coroutine endpoint APIs remain TODO |
| Coroutine benchmark | Native Asio `awaitable`/`co_spawn` compares schedulers |
| Separate ark framework | Public database coroutine modules; see the [integration boundary](../project/production-readiness.md#framework-coroutine-and-database-boundary) |

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

Bind `co_spawn` and timers to `runtime.get_executor()` for execution on the polling thread.
Poll while operations are pending; idle polls return zero and later posts work without a restart.

| Poll contract | Behavior |
| --- | --- |
| `poll(0)` | Executes nothing |
| Foreign-thread or reentrant call | Throws `std::logic_error` |
| Handler exception | Propagates; later polls remain valid |
| Budget | Counts Asio handlers, not elapsed time or inline calls; does not bound posting |

<Aside type="caution" title="Manual runtime lifetime">

Finish or cancel external operations and stop producers before owner-thread destruction.
The executor does not keep the context alive.

</Aside>

| Lifetime and integration | Contract |
| --- | --- |
| Active poll | Pins the context until return; releasing runtime does not cancel handlers |
| Destruction | Releases unexecuted captures; **not** an IO drain, join or dynamic-library unload barrier |
| Dispatch | Use runtime's poll entry point; do not run or stop the underlying context |
| Native endpoint callbacks | Use their own executors; runtime does not change their callback thread unless explicitly bridged to this executor |

## Asio References

- [Threads and Asio](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/threads.html)
- [Strands](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/strands.html)
- [C++20 Coroutines](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/composition/cpp20_coroutines.html)

See the [performance comparison](../performance/io-comparison.md) for throughput, RTT and CPU results.
