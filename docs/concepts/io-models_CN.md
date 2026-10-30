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

| 调度器 | 通道约定 |
| --- | --- |
| 内置 `arknet::io_pool`（别名 `arknet::iopool`） | 每通道一个 context 和工作线程；首通道监听，存在其他通道时由其他通道处理 session |
| 外部 context | 支持一个或多个运行线程；arknet 为每个传入通道创建 strand，不停止宿主 context |
| 直接构造 `arknet::io_t` | 默认使用 strand；`serialize = false` 要求 context 只有一个运行线程，内置池采用此设置 |

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

<Aside type="caution" title="保持外部 context 有效">

取消回调可能仍持有另一 context 的 Session 或 strand。网络回调中只请求停止，由拥有对象的线程等待完成。

</Aside>

<Steps>

<Step title="停止网络对象">

所有对象停止前，保持 context、运行线程和 work guard 有效。

</Step>

<Step title="释放 work guard 并排空运行线程">

等待所有宿主运行线程退出后，再销毁任一 context。详见[发送与生命周期](sending_CN.md)。

</Step>

</Steps>

| 自有端点回收 | 约定 |
| --- | --- |
| 关闭后在 IO 回调中释放最后一个引用 | 内部回收线程排空并回收池，避免等待当前线程自身退出 |
| 析构已关闭端点 | 不再投递停止操作 |
| 回收线程范围 | 同一可执行文件或动态库的自有端点共用；不执行网络业务回调 |

外部 context 的有效期仍由调用方负责。

## 协程业务

协程表达异步控制流，不创建线程或自动分配 CPU 工作。

| 执行阶段 | 约定 |
| --- | --- |
| `co_await` 等待 | 释放运行线程；挂起点之间的计算仍占用该线程 |
| 恢复 | 可能切换到同一 context 的其他运行线程 |
| 共享连接状态 | 在 strand 上启动；单个顺序协程不能串行化其他操作 |

| 业务负载 | 建议起点 | 原因 |
| --- | --- | --- |
| IO 密集型网关、聊天服务、HTTP client | 1 个 io_context、多个线程、每个连接独立 strand | 连接活跃程度变化时可以使用空闲线程 |
| 小型代理或控制服务 | 1 个 io_context、1 个线程 | 状态处理串行，调度开销较低 |
| 分区游戏会话、租户工作进程 | 每个分区独立 io_context 和线程 | 状态归属和业务分配明确 |
| 压缩、解析、耗时请求计算 | 任意 IO 模型，加有容量限制的 CPU 执行器 | 避免计算阻塞其他 IO；异步等待计算结果让 IO 线程继续工作 |

应按实际连接分布和回调成本测量，以上只是选型起点，不是性能排名。
两种模型中，繁忙连接的状态机均由 strand 串行化，增加运行线程不能将其并行化。

| 接口 | 可用范围 |
| --- | --- |
| arknet 端点与原生数据库客户端 | 回调 API；专用协程端点 API 仍为 TODO |
| 协程基准 | 使用原生 Asio `awaitable`/`co_spawn` 比较调度器 |
| 独立 ark 框架 | 已有公开数据库协程模块，不能视为 arknet API；见[接入边界](../project/production-readiness_CN.md#框架协程与数据库边界) |

## 由调用方驱动执行

`arknet::manual_runtime` 在库内持有 context 和 work guard，不启动执行用户 handler 的工作线程。
Asio 的平台服务仍可能使用内部辅助线程，例如 Windows 定时器或 DNS 解析。
由创建线程调用 `poll(max_handlers)`，例如在应用的 update 循环中派发。
其他线程可以通过标准 Asio executor 投递完成通知：

```cpp
#include <arknet/base/manual_runtime.hpp>

arknet::manual_runtime runtime;
asio::post(runtime.get_executor(), [] { /* 在调用 poll 的线程中执行。 */ });
runtime.poll(64); // 最多执行 64 个就绪的 Asio handler，不等待新工作。
```

将 `co_spawn` 和 timer 绑定到 `runtime.get_executor()`，在 poll 线程执行。
操作未完成时持续 poll；空闲时返回 0，后续投递无需重启即可执行。

| poll 约定 | 行为 |
| --- | --- |
| `poll(0)` | 不执行工作 |
| 跨线程或重入调用 | 抛出 `std::logic_error` |
| handler 异常 | 向调用方传播，后续仍可 poll |
| 上限 | 统计 Asio handler 个数，不代表耗时，不限制内联调用或投递容量 |

<Aside type="caution" title="手动 runtime 的有效期">

由拥有者线程销毁前，先完成或取消外部操作并停止投递方。executor 不持有 context 所有权。

</Aside>

| 有效期与接入 | 约定 |
| --- | --- |
| 执行中的 poll | 固定 context 到调用返回；释放 runtime 不取消 handler |
| 析构 | 释放未执行的捕获；**不构成** IO 排空、线程退出或动态库卸载屏障 |
| 派发入口 | 使用 runtime 的 poll，不自行运行或停止内部 context |
| 原生端点回调 | 仍使用自身 executor；runtime 不自动改变回调线程，需要显式转交到该 executor |

## Asio 参考文档

- [线程模型](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/threads.html)
- [Strand](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/core/strands.html)
- [C++20 协程](https://think-async.com/Asio/asio-1.38.2/doc/asio/overview/composition/cpp20_coroutines.html)

模型的吞吐量、RTT 和 CPU 对比见[性能报告](../performance/io-comparison_CN.md)。
