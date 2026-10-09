# 生命周期、发送和错误

本页说明基于回调的客户端、服务端和 Session 的运行约定。完整程序见
[快速开始](quickstart_CN.md)，调度模型见 [IO 模型](threading_CN.md)。

## 生命周期和执行上下文

启动前配置回调、协议选项和 TLS 身份。停止完成前，保持对象及回调捕获的资源存活。
接收回调中的数据 view 只在回调期间有效；需要保留数据时，复制数据。

内置 IO 线程池每个通道使用一个 context 和一个工作线程。外部 context 可以由一个
或多个线程调用 `run()`。每个外部通道使用 strand 串行执行自身回调；下一次回调
可能运行在不同的宿主线程。独立通道可以并行执行，应用仍需同步跨通道共享的状态。

客户端和服务端的停止约定如下：

| 操作 | 约定 |
| --- | --- |
| 所有者线程调用 `stop()` | 发起停止并等待完成 |
| `request_stop()` | 调度停止，可在网络回调中调用 |
| 所有者线程调用 `wait_stopped()` | 完成停止，并报告是否允许等待 |
| 在所涉及 context 或线程池的工作线程调用 `wait_stopped()` | 返回 `false`，错误为 `operation_not_supported` |

启动、发送和停止期间，外部 context、运行线程及 work guard 必须持续运行。arknet
不会停止宿主 context。先停止并销毁网络对象，再释放宿主 work guard 并等待运行
线程退出。让 `run()` 执行完已排队的完成回调后自然返回。`io_context::stop()`
会停止回调派发，不会完成这个排空过程。所有 context 都应保持存活，直到运行
线程结束；尤其要考虑回调仍持有另一个 context 的 Session 或 strand。
不要在内置 IO 线程池自己的工作线程中销毁线程池；这种情况下析构函数会终止程序。

独立的 `arknet::timer` 在构造时启动，提供 `stop()`，没有 `request_stop()` 或
`wait_stopped()`。在回调中调用 `stop()` 不会阻塞；外部 context 持续运行时，
所有者线程可以再次调用 `stop()`，等待尚未完成的处理函数退出。

## 发送和数据所有权

以下约定针对客户端和 Session 的单连接发送。服务端广播重载将数据转发给各
Session；需要逐个对端的结果时，使用每个 Session 的完成回调。

`async_send()` 对接受的字符串、string view、span 和 Asio buffer 持有数据
所有权，也会复制数据过滤器返回的 view。提交返回后，调用者可以复用源存储。
用于回显发送的接收 view 仍然只在接收回调期间有效。

返回的 `bool` 表示操作是否被接受，不表示对端已接收或处理消息。完成回调支持
`(const error_code&, size_t)`、`(size_t)` 或 `()`，也支持泛型、重载和只能移动的
可调用对象。

| 提交结果 | 返回值 | 完成回调的执行位置 |
| --- | --- | --- |
| 输入无效、对象未连接或队列达到上限 | `false` | 在调用线程立即执行，返回错误和零字节 |
| 操作被接受 | `true` | 传输完成或取消后，在对象的 IO executor 上执行 |

操作被拒绝时，回调可能在 `async_send()` 返回前执行。因此，不能假设全部完成回调
都会被投递，也不要在持有回调需要获取的锁时提交发送。优先使用带错误和字节数的
回调，读取本次操作的错误。传输完成不表示对端应用已经处理消息；需要确认时，
由应用协议提供响应。

### 背压

每个对象默认最多排队 16 MiB 有效载荷和 1024 个操作。空数据也计入操作数量。

| API | 含义 |
| --- | --- |
| `set_max_send_buffer_size(bytes)` | 设置排队有效载荷的字节上限 |
| `get_max_send_buffer_size()` | 读取字节上限 |
| `get_queued_send_buffer_size()` | 查询当前已预留的有效载荷字节数 |

超出任一上限时，操作被拒绝，错误为 `no_buffer_space`。查询值只是当前状态，
不会为后续发送预留空间。应用可以暂停生产、减少并发，或在完成回调释放容量后按
自身策略重试。提高上限会增加可能的内存占用，不能提高对端的接收速率。

### future 和 send()

future 重载返回 `std::future<std::pair<error_code, size_t>>`：

```cpp
auto completion = client.async_send(std::string("hello"), asio::use_future);
// 在所有者线程等待，同时保持 IO context 持续运行：
auto [error, bytes] = completion.get();
```

不要在同一个 context 的工作线程等待未完成的发送或 post future；即使当前回调
位于另一个 strand，也遵守此约定。

`send()` 会提交异步操作。在所涉及 context 的工作线程以外调用时，它会等待并返回
传输字节数。在工作线程调用时，如果操作尚未完成，异步操作继续执行，`send()`
返回零并设置 `in_progress`。立即完成的失败会报告实际错误。回调中使用
`async_send()`，显式处理完成结果。

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

使用已有 key 启动定时器会替换原定时器。`stop_timer(key)` 取消指定定时器；
`stop_all_timers()` 取消全部带 key 的定时器。`stop_all_timed_events()` 及其别名
`stop_all_timed_tasks()` 取消延迟 post。取消操作在 executor 上调度，底层等待处理
函数仍需完成。默认情况下，取消后不调用用户回调。被取消的 post future 如果因
回调未执行而失去 callable，会就绪；读取结果时抛出 `std::future_error`，错误码为
`std::future_errc::broken_promise`。
定义 `ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR` 可在错误时调用定时器回调；
此时在回调中检查 `get_last_error()`，并确保全部翻译单元使用相同宏配置。

客户端默认启用自动重连，重试延迟为一秒。启动前配置：

```cpp
client.set_auto_reconnect(true, std::chrono::seconds(2));
// 或关闭自动重试：
client.set_auto_reconnect(false);
```

重连成功后，连接的代次会更新。旧队列中的发送不能重放到新连接；过期操作以
`operation_aborted` 完成。在 connect 回调中检查错误后，再恢复应用状态。

## 错误和 TLS

`get_last_error()` 使用线程局部存储。在回调内或报告错误的同步调用之后立即读取
并复制错误值；稍后在另一个线程读取，无法得到原操作的错误。发送优先使用显式的
完成错误或 future 结果。

```cpp
client.bind_connect([]
{
    const arknet::error_code error = arknet::get_last_error();
    if (error) {
        // 记录错误，或将错误值传入应用状态。
    }
});
```

TLS 客户端默认验证证书链和 DNS/IP 身份，并为 DNS 主机设置 SNI。服务端需要可用
证书及匹配的私钥。mTLS 还需信任客户端 CA，并在服务端设置
`verify_peer | verify_fail_if_no_peer_cert`。显式设置 `verify_none` 会关闭验证。
仓库中的证书和私钥仅用于测试。

操作失败时，按[故障排查](troubleshooting_CN.md)检查。当前验证证据和范围限制见
[测试](testing_CN.md)。
