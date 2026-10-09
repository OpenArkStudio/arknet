# UDP 报文大小与突发丢包

Mac mini M4 Pro 上的 Release IPv4 回环测试：16 个客户端、1 个 context、1 个线程，
无额外业务计算，测量 1 秒、预热 0.25 秒。每种后端运行六种包体、三档窗口，
各重复三次：**36 次通过、18 次失败**。

![不同包体和请求窗口下的 IPv4 UDP 丢失回显数](assets/udp-limits-loss.svg)

图中是每次运行测量阶段的丢失回显数。三次重复的丢包计数完全相同，两种后端
也一致。预热错误保留在原始 JSON 中；失败配置不参与吞吐量比较。

- 所有包体在窗口 1 下都通过，包括 **65,507 字节**；1,472 字节在窗口 16 和 64 下也通过。
- 窗口 64 下，8,192 字节每次丢失 512 条回显；16,356／16,357／16,384 字节各丢失 767 条。
  65,507 字节在窗口 16 下丢失 191 条，在窗口 64 下丢失 959 条。

## 包体与 MTU 上限

| 普通 UDP 包体 | IPv4 | IPv6 |
| --- | ---: | ---: |
| 协议最大值 | 65,507 = 65,535 − 20 − 8 | 65,527 = 65,535 − 8 |
| MTU 1,500 时的免分片上限 | 1,472 = 1,500 − 20 − 8 | 1,452 = 1,500 − 40 − 8 |
| MTU 16,384 时的数学预算 | 16,356 | 16,336 |

这里假定 IPv4 头为 20 字节、IPv6 基础头为 40 字节，UDP 头为 8 字节。
IP options 或扩展头会减小可用包体；表中不包含 IPv6 jumbogram。
IPv6 路由器不执行分片。这组诊断只测量 IPv4，不能据此确定 IPv6 负载上限。

`lo0` 的 MTU 为 16,384，但更大报文成功收发时，IPv4 分片和重组计数仍保持为零。
因此数学 MTU 边界不是这条回环路径上实测的分片阈值；跨主机报文大小应以实际
路径 MTU 为依据。

## 内核证据

| 后端 | 接收缓冲溢出计数：开始 → 结束 | 增量 | IPv4 分片／重组增量 |
| --- | ---: | ---: | ---: |
| Standalone Asio | 27,666 → 51,444 | 23,778 | 0 / 0 |
| Boost.Asio | 51,444 → 75,222 | 23,778 | 0 / 0 |

每种后端在测量阶段记录 11,889 条丢失回显。内核计数覆盖整段测试，包括预热，
并且是系统全局计数。它记录了突发流量期间的接收缓冲溢出；没有 IPv4 分片
计数支持“分片导致这些丢包”的解释。发送拒绝、发送完成错误和内容错误均为零。

已配置 socket 的实际缓冲区为 **64 KiB 发送／4 MiB 接收**。
`net.inet.udp.maxdgram=9216` 与 `net.inet.udp.recvspace=786896` 始终未变；
65,507 字节回显成功，说明 9,216 不是这些已配置 socket 的有效包体硬上限。
16 KiB 在窗口 16 下通过，窗口 64 下发生缓冲溢出：报文是否合法与突发容量
是两件独立的事。

## 复现

按 [UDP 构建说明](../udp/performance_CN.md#构建与执行)编译后，直接运行独立 C++ 用例：

```sh
build-perf/benchmarks/arknet_loopback_benchmark \
  --protocol udp --payload 65507 --clients 16 --window 1 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
```

归档的[Standalone 命令与内核快照](https://github.com/OpenArkStudio/arknet/tree/main/benchmarks/results/macmini-m4pro-20261010-udp-limits)
也保留了完整 shell 矩阵和 Boost 命令。可选离线图表工具使用[报告环境](../testing_CN.md#指标与图表)：

```sh
build/report-venv/bin/python scripts/udp_limits_report.py \
  --input-dir benchmarks/results/macmini-m4pro-20261010-udp-limits \
  --output docs/performance/assets/udp-limits-loss.svg
```

报告工具检查验证归档数据完整性，并拒绝异常输入；不会启动网络测试：

```sh
python3 -m unittest discover -s scripts -p 'test_udp_limits_report.py' -v
```

[Standalone 原始 JSON](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-udp-limits/standalone.json) ·
[Boost 原始 JSON](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-udp-limits/boost.json) ·
[图表数值](assets/udp-limits-loss.json ':ignore') · [环境与测量定义](../testing_CN.md)

源码 SHA-256：`fa69d6ce97745281290a263cb0f2515470ede0edb77f5132ce6e3ac7ae7cd9b4`。
