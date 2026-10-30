# Use an External IO Context

Use application-owned IO when arknet should share your event loop or worker
threads. Configure a work guard and runners before a blocking `start()` call.
The application retains ownership of every supplied context.

## Configure execution lanes

```cpp
asio::io_context context;
auto work = asio::make_work_guard(context);
std::vector<asio::io_context*> lanes(5, &context);
arknet::tcp_server server(1024, 16 * 1024 * 1024, lanes);
```

Run `context.run()` on the desired application threads. The example creates five
serialized lanes: independent lanes can run concurrently with multiple runners.

| Supplied IO | Execution contract |
| --- | --- |
| One `io_context&` passed directly to a server | One serialized lane shared by its sessions |
| Distinct contexts, each with its own thread | Partitioned execution |
| UDP server, either layout | Sessions share its socket and receive lane |

Choose a layout using [IO models](../concepts/io-models.md).


## Stop and drain

<Steps>

1. From an IO callback, use `request_stop()` and return.
2. On the owner thread, call `stop()` or `wait_stopped()` while the runners remain active.
3. Destroy network objects, then release the work guard with `work.reset()`.
4. Join every runner before destroying any context.

</Steps>

Allow `run()` to drain queued completion handlers and return naturally.
`io_context::stop()` interrupts dispatch and does not complete that drain.
A queued handler in one context can retain a session or strand owned by another context.

[Lifecycle contract](../concepts/lifecycle.md) · [External-context regression tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/tcp.cpp) · [Performance comparison](../performance/io-comparison.md)
