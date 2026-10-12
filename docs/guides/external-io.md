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

Run `context.run()` on the desired number of application threads. This example
creates five serialized lanes on one context. With multiple runners, independent
lanes can execute concurrently; a lane's handlers still run serially. Passing a
single `io_context&` directly to a server instead creates one lane shared by its sessions.

For partitioned execution, supply distinct contexts and run each on its own
thread. UDP server sessions share the server socket and receive lane in either
model. See [IO models](../concepts/io-models.md) before selecting a layout.

## Stop and drain

1. From an IO callback, use `request_stop()` and return.
2. On the owner thread, call `stop()` or `wait_stopped()` while the runners remain active.
3. Destroy network objects, then release the work guard with `work.reset()`.
4. Join every runner before destroying any context.

Allow `run()` to drain queued completion handlers and return naturally.
`io_context::stop()` interrupts dispatch and does not complete that drain.
A queued handler in one context can retain a session or strand owned by another context.

[Lifecycle contract](../concepts/lifecycle.md) · [External-context regression tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/tcp.cpp) · [Performance comparison](../performance/io-comparison.md)
