# Concepts

These concepts describe contracts shared by the protocol modules. Read them when
deciding how to own connections, schedule handlers and retain data.

- [Clients, servers and sessions](endpoints.md): responsibilities and peer identity.
- [IO models and coroutines](io-models.md): runners, strands and workload choices.
- [Lifecycle and execution](lifecycle.md): startup, cancellation and safe destruction.
- [Sending, ownership and backpressure](sending.md): queue admission, completion and borrowed data.

For concrete configuration tasks, use the [guides](../guides/index.md).
