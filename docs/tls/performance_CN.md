# TLS 性能

## 本机 TCPS 测量结果

Release 本机回环测试：TLS 握手后发送 1024 字节消息、16 个客户端、窗口 16，无额外业务计算。

![不同 IO 模型下的 TCPS 吞吐量](../performance/assets/tcps-work0-throughput.svg)

![不同 IO 模型下的 TCPS p99 RTT](../performance/assets/tcps-work0-latency.svg)

### 本机结论

- 两种后端都以 4 个 io_context 各 1 个线程的吞吐量中位数最高、p99 中位数最低。
- 单客户端、窗口 1 时，两种后端则都以 1 个 io_context、1 个线程的吞吐量中位数最高、p99 中位数最低。

[完整 TCPS 报告](../performance/tcps_CN.md) · [WSS 报告](../performance/wss_CN.md) · [HTTPS 报告](../performance/https_CN.md)

[测试环境](../performance/overview_CN.md) · [指标与统计口径](../testing_CN.md)

[包体、批量与执行方式对比](../performance/dimensions_CN.md#tcps)

## 构建与复现

在仓库根目录使用 Release 构建。使用 Boost 时，将 standalone 头文件路径参数替换为 `-DARKNET_USE_BOOST_ASIO=ON`。两个后端应使用相同 OpenSSL 版本、主机和构建选项。

命令采用单配置 generator。使用 Visual Studio 时，在构建命令中加入 `--config Release`，并运行 `benchmarks/Release/` 下的可执行文件。Windows 可执行文件带有 `.exe` 后缀。

```sh
cmake -S . -B build-perf-tls -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_ENABLE_SSL=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf-tls --target arknet_loopback_benchmark --parallel 3
for repetition in 1 2 3; do
  for protocol in tcp tcps websocket wss http https; do
    build-perf-tls/benchmarks/arknet_loopback_benchmark \
      --protocol "$protocol" --certs tests/certs \
      --payload 1024 --clients 16 --window 16 \
      --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1 \
      > "build-perf-tls/${protocol}-${repetition}.json" || exit 1
  done
done
```

这个循环为每种明文／TLS 协议用独立 C++ 进程测量三次，使用 1024 字节消息、16 个客户端、
窗口 16、1 个 context 和 1 个线程，分别保存每次结果。将 `--io-threads`
改为 2 或 4 可比较共享 context，使用 `--io-model sharded --io-threads 4` 可比较
4 个 context 各 1 个线程。`--work 10000` 表示每次回显增加 10,000 次确定性运算，
`0` 表示无额外业务计算。完整 shell 矩阵见[测试](../testing_CN.md#性能矩阵)。
同一连接的加密仍在串行通道中执行。

服务端使用测试证书与私钥，客户端信任测试 CA，并校验证书链和 IP 身份。公开证书仅用于本地测试；mTLS 正确性由 [TLS 测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/tls.cpp)单独覆盖。

比较 TCP/TCPS、WebSocket/WSS 或 HTTP/HTTPS 时，保持消息大小、并发、计算量、后端和线程模型一致。TCP 使用 `use_dgram`，WebSocket 使用二进制帧，HTTP 使用持久连接。

测试未覆盖握手速率、频繁重连、会话恢复、证书轮换、密码套件或 mTLS 吞吐量，JSON 也不记录协商的密码套件。端到端差异包含协议解析与加密开销，稳态回环结果不能推算频繁建连能力或跨主机容量。

另见 [IO 模型](../threading_CN.md)及[基准源码](https://github.com/OpenArkStudio/arknet/blob/main/benchmarks/loopback.cpp)。
