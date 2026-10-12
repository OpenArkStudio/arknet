# 接入外部 IO Context

需要与应用共享事件循环或运行线程时，使用应用持有的 IO。阻塞式 `start()`
调用前，先设置 work guard 并启动运行线程。传入的 context 始终由应用管理。

## 配置执行通道

```cpp
asio::io_context context;
auto work = asio::make_work_guard(context);
std::vector<asio::io_context*> lanes(5, &context);
arknet::tcp_server server(1024, 16 * 1024 * 1024, lanes);
```

在所需数量的应用线程中运行 `context.run()`。示例在一个 context 上创建五个
串行通道。多个运行线程可并行执行独立通道，单个通道的回调仍串行执行。
直接向服务端传入一个 `io_context&` 时，只创建一个由 Session 共用的通道。

分片执行则传入不同的 context，并分别使用独立线程运行。无论选择哪种模型，
UDP 服务端 Session 都共用服务端 socket 和接收通道。选型前阅读
[IO 模型](../concepts/io-models_CN.md)。

## 停止与排空

1. IO 回调中调用 `request_stop()`，然后返回。
2. 保持运行线程工作，由所有者线程调用 `stop()` 或 `wait_stopped()`。
3. 销毁网络对象，再通过 `work.reset()` 释放 work guard。
4. 等待全部运行线程退出，最后销毁 context。

让 `run()` 排空已排队的完成回调后自然返回。`io_context::stop()` 会中断派发，
不能完成排空。一个 context 中排队的回调可能仍持有另一个 context 的 Session 或 strand。

[生命周期约定](../concepts/lifecycle_CN.md) · [外部 context 回归测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/tcp.cpp) · [性能比较](../performance/io-comparison_CN.md)
