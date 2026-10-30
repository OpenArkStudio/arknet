# IO 模型与协程性能对比

## 原生 Asio 协程基线

原生 Asio TCP 协程 Release 本机回环测试：1024 字节消息、16 个客户端、窗口 16；延迟为批次 RTT。

### 无额外业务计算

![不同 IO 模型下的协程吞吐量](assets/coroutine-work0-throughput.svg)

![不同 IO 模型下的原生 Asio 协程批次 p99 RTT](assets/coroutine-work0-latency.svg)

- 两种依赖方案都以 1 个 io_context、4 个线程的吞吐量中位数最高，1 个 io_context、2 个线程的批次 p99 中位数最低。
- Standalone Asio 的 1 个 io_context、2 个线程与 1 个 io_context、4 个线程的批次 p99 运行范围重叠，不能据此确认稳定的尾延迟优势。

### 每次回显增加 10,000 次确定性运算

![加入计算后的原生 Asio 协程吞吐量](assets/coroutine-work10000-throughput.svg)

![加入计算后的原生 Asio 协程批次 p99 RTT](assets/coroutine-work10000-latency.svg)

- 两种依赖方案都以 4 个 io_context 各 1 个线程的吞吐量中位数最高、批次 p99 中位数最低。
- 1 个 io_context、4 个线程与 4 个 io_context 各 1 个线程的批次 p99 运行范围在两种依赖方案中均有重叠。

[完整协程基线报告](coroutine_CN.md) · [包体、批量与执行方式对比](dimensions_CN.md#native-tcp)

[测试环境](overview_CN.md) · [指标与统计口径](methodology_CN.md) · [TCP 回调结果](tcp_CN.md)

## 复现比较

<Steps>

<Step title="编译并运行">

`benchmarks/loopback.cpp` 使用 arknet 回调，`benchmarks/coroutine.cpp` 使用原生 Asio 回调／协程；各有独立 `main`。用 CMake 编译后直接运行：

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=OFF \
  -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf --target arknet_coroutine_benchmark --parallel 3
build-perf/benchmarks/arknet_coroutine_benchmark \
  --execution coroutine --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --work 0 --warmup 0.25 --seconds 1
```

</Step>

<Step title="匹配配置与 RTT 口径">

这个例子使用 1 个 context、4 个线程、1024 字节消息、16 个客户端和窗口 16，
向标准输出写入 JSON。使用 Boost 时，将 Asio 头文件参数替换为
`-DARKNET_USE_BOOST_ASIO=ON`。使用 Visual Studio 时，加入 `--config Release`，
并运行 `benchmarks/Release/` 下带 `.exe` 后缀的程序。

`--io-model shared` 为多线程共用一个 io_context，`sharded` 为每线程独立 io_context；`--io-threads 1` 为单 context、单线程。`--execution callback` 运行匹配的原生回调；`--work 10000` 每次回显增加 10,000 次确定性运算，`0` 不加计算。模拟负载不代表真实解析器或加密算法。

<Aside type="caution" title="RTT 比较范围">

仅比较同程序、依赖、包体、并发和计算量的拓扑。原生 Asio 按固定批次收发，`--window` 大于一时测批次 RTT；arknet 回调持续补充窗口，测逐消息 RTT，不能据此断言协程加速比。

</Aside>

[匹配对比](dimensions_CN.md#native-tcp)使用同一原生 Asio TCP 程序的回调／协程：服务端读完整批，逐条计算，再整批回写。窗口 1 测单消息 RTT，更大窗口测批次 RTT。

</Step>

<Step title="重复配置">

用独立 C++ 进程重复配置，或使用[测试](methodology_CN.md#%E6%80%A7%E8%83%BD%E7%9F%A9%E9%98%B5)中的维护者 shell 矩阵。
只有批量工具需要 `jq`、GNU `timeout` 和 `shasum`；直接运行 C++ 功能测试和单项基准
不需要这些工具。

<Aside type="caution" title="适用范围">

原始结果与命令见[测试与性能](methodology_CN.md)。短时回环测量不能预测广域网延迟或生产容量。

</Aside>

</Step>

</Steps>
