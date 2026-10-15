# RUDP 性能

[本地 RUDP 报告](../../performance/rudp_CN.md)对比仅 ARQ、固定 FEC 与自适应 FEC，
覆盖三种 IO 模型、包体、在途请求及随机／突发丢包。图表展示吞吐量、p99 RTT、
CPU 占用、网络开销、恢复与重传。每种常规配置重复三次，误差线表示最小值与最大值。

正式基准程序为 `benchmarks/rudp.cpp`，编译后直接执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --target arknet_rudp_benchmark
build/benchmarks/arknet_rudp_benchmark --payload 4096 --clients 8 --window 4 \
  --io-model shared --io-threads 4 --warmup 2 --seconds 3 --rounds 3 \
  --fec adaptive --profile random --loss-percent 5 --delay-ms 10
```

程序输出 JSON，检查消息内容、顺序、完成通知与收尾，失败时返回非零。
编译后直接运行，不需要 Python、Ruby 或 Node.js。

批量测量可用 `benchmarks/run_rudp_fec.sh`，复现参数与原始数据见报告。
`benchmarks/report_rudp_fec.mjs` 仅用于离线生成文档与 SVG，不是测试入口。
当前公开 API 使用回调，没有 RUDP 协程性能测量；本机模拟结果不表示 WAN 容量。
