# 使用定时器与 post

## 定时器、post 和重连

`post(callback)` 将任务投递到对象的 executor。已经位于该 executor 时，
`dispatch(callback)` 可以立即执行。`post(callback, delay)` 使用 steady timer
延迟执行。future 重载返回回调的执行结果。

```cpp
using namespace std::chrono_literals;
arknet::timer timer;
timer.post([] { /* 立即投递的任务 */ });
timer.start_timer("heartbeat", 1s, [] { /* 重复任务 */ });
timer.start_timer("once", 100ms, 1, [] { /* 执行一次 */ });
// 稍后，在所有者线程调用：
timer.stop();
```

| 定时器操作 | 效果 |
| --- | --- |
| 使用已有 key 启动 | 替换原定时器 |
| `stop_timer(key)` | 取消指定 key |
| `stop_all_timers()` | 取消全部带 key 的定时器 |
| `stop_all_timed_events()`／别名 `stop_all_timed_tasks()` | 取消延迟 post |

取消在 executor 上调度，底层等待 handler 仍须完成。默认取消后不调用用户回调。
被取消的 post future 若因回调被抑制而失去 callable，会就绪；取结果时抛出
`std::future_error`，错误码为 `std::future_errc::broken_promise`。

<Aside type="caution" title="定时器错误回调">

`ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR` 启用错误时的定时器回调。
在回调中检查 `get_last_error()`；全部翻译单元须使用相同宏配置。

</Aside>
