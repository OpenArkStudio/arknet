# RUDP 性能

[本地 RUDP 报告](../../performance/rudp_CN.md)对比包体大小、在途请求和三种
IO 结构，展示吞吐量、RTT 与 CPU 占用。每种配置重复三次，误差线表示最小值与最大值。

正式基准程序为 `benchmarks/rudp.cpp`，编译后直接执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --target arknet_rudp_benchmark
build/benchmarks/arknet_rudp_benchmark --payload 4096 --clients 8 --window 4 \
  --io-model shared --io-threads 4 --warmup 1 --seconds 3 --rounds 3
```

程序输出 JSON，校验失败时返回非零，不需要 Python 或 Ruby。批量 shell 命令
和原始数据见报告。当前公开 API 使用回调，没有 RUDP 协程性能测量，也不据此
推断 WAN 容量。丢包恢复由独立的正确性测试验证。
