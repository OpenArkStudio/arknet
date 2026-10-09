# IO 模型与协程

## 原生 Asio 协程基线

原生 Asio TCP 协程 Release 本机回环测试：1024 字节消息、16 个客户端、窗口 16；延迟为批次 RTT。

### 无额外业务计算

![不同 IO 模型下的协程吞吐量](performance/assets/coroutine-work0-throughput.svg)

![不同 IO 模型下的原生 Asio 协程批次 p99 RTT](performance/assets/coroutine-work0-latency.svg)

- 两种后端都以 1 个 io_context、4 个线程的吞吐量中位数最高，1 个 io_context、2 个线程的批次 p99 中位数最低。
- standalone 的 1 个 io_context、2 个线程与 1 个 io_context、4 个线程的批次 p99 运行范围重叠，不能据此确认稳定的尾延迟优势。

### 每次回显增加 10,000 次确定性运算

![加入计算后的原生 Asio 协程吞吐量](performance/assets/coroutine-work10000-throughput.svg)

![加入计算后的原生 Asio 协程批次 p99 RTT](performance/assets/coroutine-work10000-latency.svg)

- 两种后端都以 4 个 io_context 各 1 个线程的吞吐量中位数最高、批次 p99 中位数最低。
- 1 个 io_context、4 个线程与 4 个 io_context 各 1 个线程的批次 p99 运行范围在两种后端中均有重叠。

[完整协程基线报告](performance/coroutine_CN.md) · [包体、批量与执行方式对比](performance/dimensions_CN.md#native-tcp)

[测试环境](performance/overview_CN.md) · [指标与统计口径](testing_CN.md) · [TCP 回调结果](performance/tcp_CN.md)

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
strand。详见 [发送与生命周期](runtime_CN.md)。

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

## 复现比较

`benchmarks/loopback.cpp` 是 arknet 回调程序，`benchmarks/coroutine.cpp` 是原生
Asio 回调／协程程序。两者都有独立 `main`，使用 CMake 编译后直接运行 C++ 可执行文件：

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf --target arknet_coroutine_benchmark --parallel 3
build-perf/benchmarks/arknet_coroutine_benchmark \
  --execution coroutine --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --work 0 --warmup 0.25 --seconds 1
```

这个例子使用 1 个 context、4 个线程、1024 字节消息、16 个客户端和窗口 16，
向标准输出写入 JSON。使用 Boost 时，将 Asio 头文件参数替换为
`-DARKNET_USE_BOOST_ASIO=ON`。使用 Visual Studio 时，加入 `--config Release`，
并运行 `benchmarks/Release/` 下带 `.exe` 后缀的程序。

`--io-model` 中的 `shared` 表示同一 io_context 由多个线程运行，`sharded`
表示每个线程使用独立 io_context。设置 `--io-threads 1` 可测试单 context、单线程；
`--execution callback` 运行匹配的原生回调模式。`--work 10000` 表示每次回显增加 10,000 次
确定性运算，`0` 表示无额外业务计算。这是模拟负载，不代表真实解析器或加密算法。
只比较同一测试程序、后端、负载大小、并发配置和计算量下的不同拓扑。
Asio 协程测试程序按固定大小批次发送和接收；`--window` 大于一时记录批次 RTT。
arknet 回调测试程序持续补充窗口，记录单条消息 RTT。两者的吞吐量和延迟
不能用来断言协程相对回调的加速比。

扩展对比使用同一原生 Asio TCP 程序的回调和协程模式。服务端完整读取一批数据，
逐条执行指定计算后整批回写。窗口为 1 时记录单消息 RTT，更大窗口记录批次 RTT。
见[匹配对比](performance/dimensions_CN.md#native-tcp)。

用独立 C++ 进程重复配置，或使用[测试](testing_CN.md#性能矩阵)中的维护者 shell 矩阵。
只有批量工具需要 `jq`、GNU `timeout` 和 `shasum`；直接运行 C++ 功能测试和单项基准
不需要这些工具。

原始结果和测试命令见 [测试与性能](testing_CN.md)。短时回环测量
不能预测广域网延迟或生产容量。

## Asio 参考文档

- [线程模型](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/threads.html)
- [Strand](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/strands.html)
- [C++20 协程](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/composition/cpp20_coroutines.html)
