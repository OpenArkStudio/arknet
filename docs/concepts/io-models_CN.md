# IO 模型与协程

## 执行模型

Asio 允许一个或多个线程调用 `io_context::run()`。单个运行线程自然保证回调
串行执行。多个运行线程可以同时执行不同回调；strand 则串行执行关联的回调。
strand 不会把连接固定到某个操作系统线程。

| io_context 与线程 | 适用业务 | 代价 |
| --- | --- | --- |
| 1 个 io_context、1 个线程 | IO 负载较小的服务、控制进程、有序状态机 | 耗时回调会阻塞所有连接 |
| 1 个 io_context、多个线程和独立 strand | 大量连接且活跃程度不均衡 | 多个线程调度同一 io_context 时存在竞争，回调可能切换线程 |
| 多个 io_context 各 1 个线程 | 分区稳定、连接负载可预测的业务 | 繁忙分区无法借用其他分区的线程 |

内置 `arknet::io_pool` 为每个 IO 通道提供一个 context 和一个工作线程。服务端
的第一个通道处理监听；存在其他通道时，session 使用其他通道。
`arknet::io_pool` 是公开调度器名称，`arknet::iopool` 保留为别名。
外部 context 可以由一个或多个线程运行。arknet 为每个传入的外部通道创建
strand，不会停止宿主 context。
直接构造的 `arknet::io_t` 默认使用 strand。显式设置 `serialize = false`
要求对应 context 只有一个运行线程；内置 IO pool 使用这一方式。

直接向 server 传入一个 context 时只会创建一个通道，session 共享该通道的
strand。传入多个通道可以让不同 session 的回调并行执行，即使这些通道使用
同一个 context：

```cpp
asio::io_context context;
auto work = asio::make_work_guard(context);
std::vector<asio::io_context*> lanes(5, &context);
arknet::tcp_server server(1024, 16 * 1024 * 1024, lanes);
// Run context.run() on the desired number of host threads.
```

向多个 client 传入同一个外部 context 时，每个 client 都有自己的通道。
UDP server 的 session 共享数据报 socket 和串行通道，因此增加运行线程
不会让同一个 UDP server 的接收回调并行执行。
多个独立通道共同访问用户状态时需要同步；异步操作之间不能依赖线程局部状态。

所有网络对象停止之前，context、运行线程和 work guard 都必须保持有效。
先停止网络对象，再释放 work guard，最后等待宿主线程退出。网络回调中应请求
停止，由拥有对象的线程等待停止完成。销毁任一 context 前，应等待所有运行线程
排空回调；某个 context 中的取消回调可能仍持有另一个 context 的 Session 或
strand。详见 [发送与生命周期](sending_CN.md)。

自有端点完成关闭后，最后一个引用可能在 IO 回调中释放。arknet 将自有线程池的
排空与回收转交给内部回收线程，避免等待当前线程自身退出。已完成关闭的端点析构
不会再投递停止回调。同一可执行文件或动态库中的自有端点共享回收线程；该线程
不执行网络业务回调。外部 context 的有效期仍由调用方负责。

## 协程业务

协程改变异步控制流的写法，不会创建线程或自动分配 CPU 工作。
`co_await` 在等待操作时释放运行线程，但两个挂起点之间的计算仍占用该线程。
同一 io_context 上的协程可能在另一个运行线程恢复。如果其他操作也访问同一连接
状态，应在 strand 上启动协程；单个顺序协程不能保证其他操作与它串行执行。

| 业务负载 | 建议起点 | 原因 |
| --- | --- | --- |
| IO 密集型网关、聊天服务、HTTP client | 1 个 io_context、多个线程、每个连接独立 strand | 连接活跃程度变化时可以使用空闲线程 |
| 小型代理或控制服务 | 1 个 io_context、1 个线程 | 状态处理串行，调度开销较低 |
| 分区游戏会话、租户工作进程 | 每个分区独立 io_context 和线程 | 状态归属和业务分配明确 |
| 压缩、解析、耗时请求计算 | 任意 IO 模型，加有容量限制的 CPU 执行器 | 避免计算阻塞其他 IO；异步等待计算结果让 IO 线程继续工作 |

这些是选型起点，不是固定性能排名。单个繁忙连接在两种模型中都会被 strand
串行化，增加 IO 线程不能把它的状态机并行化。应测试实际连接分布和回调成本。
首版 arknet 端点提供回调接口；协程基准使用原生 Asio `awaitable` 和
`co_spawn` 比较调度模型。公开协程端点 API 延后提供。

## Asio 参考文档

- [线程模型](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/threads.html)
- [Strand](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/strands.html)
- [C++20 协程](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/composition/cpp20_coroutines.html)


模型的吞吐量、RTT 和 CPU 对比见[性能报告](../performance/io-comparison_CN.md)。
