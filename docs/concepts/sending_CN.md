# 发送、数据所有权与背压

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


内核 socket 缓冲与应用队列不同，见[缓冲配置](../guides/socket-buffers_CN.md)。
