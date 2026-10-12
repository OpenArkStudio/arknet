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
