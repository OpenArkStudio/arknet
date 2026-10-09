# WebSocket 性能

## 本机测量结果

Release 本机回环测试：1024 字节二进制消息、16 个客户端、窗口 16，无额外业务计算。

### WebSocket

![不同 IO 模型下的 WebSocket 吞吐量](../performance/assets/websocket-work0-throughput.svg)

![不同 IO 模型下的 WebSocket p99 RTT](../performance/assets/websocket-work0-latency.svg)

- 两种后端都以 4 个 io_context 各 1 个线程的吞吐量中位数最高。
- standalone 的最低 p99 中位数出现在 4 个 io_context 各 1 个线程；Boost 则是 2 个 io_context 各 1 个线程，且运行范围与 4 个 io_context 各 1 个线程重叠。
- 单客户端、窗口 1 时，两种后端使用 1 个 io_context、4 个线程的吞吐量中位数均低于 1 个 io_context、1 个线程。

### WSS

![不同 IO 模型下的 WSS 吞吐量](../performance/assets/wss-work0-throughput.svg)

![不同 IO 模型下的 WSS p99 RTT](../performance/assets/wss-work0-latency.svg)

- 两种后端都以 4 个 io_context 各 1 个线程的吞吐量中位数最高、p99 中位数最低。
- 同一负载与线程模型下，WSS 的吞吐量中位数低于明文 WebSocket。

[完整 WebSocket 报告](../performance/websocket_CN.md) · [完整 WSS 报告](../performance/wss_CN.md)

[测试环境](../performance/overview_CN.md) · [指标与统计口径](../testing_CN.md)

[包体、批量与执行方式对比](../performance/dimensions_CN.md#websocket) · [WSS 对比](../performance/dimensions_CN.md#wss)

## 复现 IO 对比

在仓库根目录运行。使用 Boost 时，将 standalone 头文件路径参数替换为 `-DARKNET_USE_BOOST_ASIO=ON`，保持其他设置一致。

命令采用单配置 generator。使用 Visual Studio 时，在构建命令中加入 `--config Release`，并运行 `benchmarks/Release/` 下的可执行文件。Windows 可执行文件带有 `.exe` 后缀。

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf --target arknet_loopback_benchmark --parallel 3
build-perf/benchmarks/arknet_loopback_benchmark \
  --protocol websocket --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
```

这个配置使用 1024 字节二进制消息、16 个客户端、窗口 16、1 个 context 和 1 个线程，
向标准输出写入 JSON。每组配置用独立进程重复三次。将 `--io-threads` 改为 2 或 4
可比较共享 context，使用 `--io-model sharded --io-threads 4` 可比较 4 个 context
各 1 个线程。`--work 10000` 表示每次回显增加 10,000 次确定性运算，`0` 表示无额外
业务计算。完整 shell 矩阵见[测试](../testing_CN.md#性能矩阵)。同一连接内部仍串行
执行，业务选择见 [IO 模型](../threading_CN.md)。

## 启用 TLS

启用 TLS 后使用 `--protocol wss --certs tests/certs`。WSS 校验测试证书，结果见 [WSS 报告](../performance/wss_CN.md)，构建方法见 [TLS 性能](../tls/performance_CN.md)。

测试在升级连接后测量二进制回显，未覆盖浏览器调度、升级速率、空闲连接、压缩、ping/pong、认证、广播或慢对端。两种后端的 Asio/Beast 版本不同，不能把差异仅归因于后端选择。

另见[测试](../testing_CN.md)、[WebSocket 测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/websocket.cpp)及[基准源码](https://github.com/OpenArkStudio/arknet/blob/main/benchmarks/loopback.cpp)。
