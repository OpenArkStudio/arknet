# Configure Automatic Reconnect

Clients enable automatic reconnect by default with a one-second retry delay.
Configure it before starting:

```cpp
client.set_auto_reconnect(true, std::chrono::seconds(2));
// Or disable automatic retry:
client.set_auto_reconnect(false);
```

<Aside type="caution" title="Reconnect does not replay sends">

Successful reconnect advances the connection generation; stale queued operations
complete with `operation_aborted` instead of replaying. Check the connect callback's
error before restoring application state.

</Aside>
