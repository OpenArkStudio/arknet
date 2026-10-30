# Use Timers and post

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

| Timer operation | Effect |
| --- | --- |
| Start with an existing key | Replace that timer |
| `stop_timer(key)` | Cancel that key |
| `stop_all_timers()` | Cancel all keyed timers |
| `stop_all_timed_events()` / alias `stop_all_timed_tasks()` | Cancel delayed posts |

Cancellation runs on the executor; underlying wait handlers must still complete.
By default, canceled waits suppress user callbacks. A canceled posted-task future
whose callable is suppressed becomes ready; retrieving it throws `std::future_error`
with `std::future_errc::broken_promise`.

<Aside type="caution" title="Callbacks on timer errors">

`ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR` enables timer callbacks on errors.
Inspect `get_last_error()` there; use the macro consistently across translation units.

</Aside>
