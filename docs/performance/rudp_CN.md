# RUDP 性能

本机 72 组配置、216 次测量全部通过，共完成 8,847,130 次消息往返；未发现消息丢失、内容错误、顺序错误或收尾超时。

## 测试环境

| 配置 | 实际参数 |
| --- | --- |
| 时间（UTC） | 2026-10-12T10:38:20Z / 2026-10-12T10:44:57Z |
| 机器 | Mac mini (Mac16,11), Apple M4 Pro, 14 个逻辑核心, 48 GiB RAM |
| 系统 | macOS 27.0.1 (26A434), arm64 |
| 编译 | Apple clang version 21.0.0 (clang-2100.1.1.101); C++20 Release -O3 -DNDEBUG |
| 后端 | Standalone Asio 1.38.2, Boost.Asio 1.90.0 |
| 流量 | 8 个客户端；IPv4 回环；回调 API |
| 测量 | 每组 3 轮，每轮预热 0.25 s、测量 1 s |
| 消息 / 在途请求 | 64, 1024, 4096, 16384 B / 1, 4, 16 |
| ARQ 参数 | MTU 1200 B；发送／接收窗口 128／512 个分片；更新周期 10 ms；拥塞控制开启 |
| 接收缓冲 | 请求 4 MiB；服务端实际 4 MiB，客户端实际 4 MiB |

服务端全部会话共享一个 UDP socket 及其 strand，接收与 ARQ 处理串行执行。4 个 io_context 各 1 个线程的配置主要分散客户端，不能据此推断服务端已并行分片。

应用消息按 ARQ MTU 分片；16 KiB 是消息大小，不是单个 UDP 数据报大小。

## 往返吞吐量

![RUDP round-trip throughput](assets/rudp-throughput.svg)

## 往返延迟

![RUDP p99 RTT](assets/rudp-rtt.svg)

## CPU 占用率

![RUDP CPU utilization](assets/rudp-cpu.svg)

CPU 包含同一进程中的客户端、服务端及全部工作线程；单核占用为 100%，整机占比需除以本机 14 个逻辑核心。

## 1024 B 数值

| 后端 / IO 模型 | 每连接在途请求 | 往返 / s | 双向负载 Mbit/s | p50 RTT / ms | p99 RTT / ms | CPU / % |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Standalone Asio / 1 个 io_context / 1 个线程 | 1 | 75,391 | 1,235.2 | 0.105 | 0.136 | 99.4 |
| Standalone Asio / 1 个 io_context / 4 个线程 | 1 | 70,225 | 1,150.6 | 0.108 | 0.184 | 261.5 |
| Standalone Asio / 4 个 io_context / 4 个线程 | 1 | 68,667 | 1,125.0 | 0.114 | 0.183 | 150.1 |
| Standalone Asio / 1 个 io_context / 1 个线程 | 4 | 73,881 | 1,210.5 | 0.427 | 0.542 | 99.3 |
| Standalone Asio / 1 个 io_context / 4 个线程 | 4 | 71,556 | 1,172.4 | 0.436 | 0.630 | 262.5 |
| Standalone Asio / 4 个 io_context / 4 个线程 | 4 | 69,501 | 1,138.7 | 0.446 | 0.610 | 149.8 |
| Standalone Asio / 1 个 io_context / 1 个线程 | 16 | 72,014 | 1,179.9 | 1.759 | 2.065 | 99.6 |
| Standalone Asio / 1 个 io_context / 4 个线程 | 16 | 68,529 | 1,122.8 | 1.816 | 2.439 | 261.2 |
| Standalone Asio / 4 个 io_context / 4 个线程 | 16 | 71,681 | 1,174.4 | 1.764 | 2.187 | 151.2 |
| Boost.Asio / 1 个 io_context / 1 个线程 | 1 | 76,859 | 1,259.3 | 0.103 | 0.121 | 99.1 |
| Boost.Asio / 1 个 io_context / 4 个线程 | 1 | 73,956 | 1,211.7 | 0.106 | 0.172 | 249.3 |
| Boost.Asio / 4 个 io_context / 4 个线程 | 1 | 70,786 | 1,159.8 | 0.112 | 0.143 | 151.1 |
| Boost.Asio / 1 个 io_context / 1 个线程 | 4 | 73,006 | 1,196.1 | 0.426 | 0.562 | 99.3 |
| Boost.Asio / 1 个 io_context / 4 个线程 | 4 | 74,345 | 1,218.1 | 0.422 | 0.538 | 250.9 |
| Boost.Asio / 4 个 io_context / 4 个线程 | 4 | 71,367 | 1,169.3 | 0.444 | 0.513 | 149.5 |
| Boost.Asio / 1 个 io_context / 1 个线程 | 16 | 74,453 | 1,219.8 | 1.721 | 1.827 | 99.4 |
| Boost.Asio / 1 个 io_context / 4 个线程 | 16 | 72,762 | 1,192.1 | 1.735 | 1.967 | 251.7 |
| Boost.Asio / 4 个 io_context / 4 个线程 | 16 | 70,930 | 1,162.1 | 1.782 | 2.116 | 151.0 |

## 结论与范围

- Standalone Asio：1024 B 吞吐最高为 75,391 往返/s，CPU 99.4%（1 个 io_context / 1 个线程，在途 1）；最低 p99 为 0.136 ms（1 个 io_context / 1 个线程，在途 1）。
- Boost.Asio：1024 B 吞吐最高为 76,859 往返/s，CPU 99.1%（1 个 io_context / 1 个线程，在途 1）；最低 p99 为 0.121 ms（1 个 io_context / 1 个线程，在途 1）。

柱高为 3 轮中位数，误差线为实际最小／最大值，不是置信区间。RTT 是各轮采样分位数的汇总，吞吐分母包含收完在途请求的时长；双向负载不含协议头。

这是本机 IPv4 回环的 1 s 短测范围，不代表 WAN 吞吐、延迟或抗丢包性能；本次没有测量协程 API。

## 复现与原始数据

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf --target arknet_rudp_benchmark
build-perf/benchmarks/arknet_rudp_benchmark --payload 1024 --clients 8 --window 16 \
  --io-model shared --io-threads 4 --seconds 1 --warmup 0.25 --rounds 3
```

维护者批量采集使用 shell；单项 C++ 程序不需要 jq、Python 或 Ruby。

```sh
bash scripts/rudp_matrix.sh --build-dir build-perf --backend standalone \
  --backend-include /path/to/asio/include --output benchmarks/results/local-rudp
```

Boost 在独立目录配置 `-DARKNET_USE_BOOST_ASIO=ON`；批量命令使用 `--backend boost --backend-include /path/to/boost/include`，其余测量参数相同。

[完整数值与测量元数据](rudp-summary.json)

[项目中的原始 JSON、命令与机器信息](https://github.com/OpenArkStudio/arknet/tree/main/benchmarks/results/macmini-m4pro-20261012-rudp)
