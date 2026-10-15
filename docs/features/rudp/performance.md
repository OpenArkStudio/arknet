# RUDP performance

The [local RUDP report](../../performance/rudp.md) compares ARQ-only, fixed FEC
and adaptive FEC across three IO models, payloads, in-flight requests and random
or burst loss. Charts show throughput, p99 RTT, CPU utilization, wire overhead,
recovery and retransmission. Each regular configuration runs three times;
whiskers show minimum and maximum.

The permanent benchmark is `benchmarks/rudp.cpp`. Build and run it directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --target arknet_rudp_benchmark
build/benchmarks/arknet_rudp_benchmark --payload 4096 --clients 8 --window 4 \
  --io-model shared --io-threads 4 --warmup 2 --seconds 3 --rounds 3 \
  --fec adaptive --profile random --loss-percent 5 --delay-ms 10
```

The program emits JSON, checks content, ordering, completion and drain, and exits
nonzero on validation failure. Compile and run it directly; Python, Ruby and
Node.js are not required.

Use `benchmarks/run_rudp_fec.sh` for batch measurements; the report records the
parameters and raw data. `benchmarks/report_rudp_fec.mjs` only generates offline
documentation and SVGs. The public API uses callbacks, with no RUDP coroutine
measurement; local simulations do not establish WAN capacity.
