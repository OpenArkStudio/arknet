# RUDP performance

The [local RUDP report](../../performance/rudp.md) compares payload sizes,
in-flight requests and three IO arrangements with throughput, RTT and CPU
utilization. Each configuration has three measurements and min/max error bars.

The permanent benchmark is `benchmarks/rudp.cpp`. Build and run it directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --target arknet_rudp_benchmark
build/benchmarks/arknet_rudp_benchmark --payload 4096 --clients 8 --window 4 \
  --io-model shared --io-threads 4 --warmup 1 --seconds 3 --rounds 3
```

The executable emits JSON and returns nonzero on validation failure. It needs
no Python or Ruby. See the report for the shell batch command and raw data.
The public API currently uses callbacks; there is no RUDP coroutine benchmark
or WAN-capacity claim. Loss recovery is covered by separate correctness tests.
