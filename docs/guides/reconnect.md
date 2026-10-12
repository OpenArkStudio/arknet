# Configure Automatic Reconnect

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
