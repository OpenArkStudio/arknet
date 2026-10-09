# 包体、批量与执行方式对比

本机 Release IPv4 回环测试：16 个客户端，三种消息大小、四种窗口及三种线程模型，每个配置重复三次。

| 配置 | 实测机器 |
| --- | --- |
| 机器型号 | Mac16,11 |
| 处理器 | Apple M4 Pro |
| 架构 | arm64 |
| 物理 / 逻辑核心 | 14 / 14 |
| 内存 | 48 GiB |
| 操作系统 | macOS 27.0.1 (26A434) |
| 内核版本 | 27.0.0 |

测量 1.0 秒，预热 0.25 秒。图表为中位数及最小／最大值，误差线不是置信区间；p99 为各次运行采样分位数的中位数。

平均进程 CPU 占用率以单核为 100%，多线程可超过 100%；旧样本标为估算，三次 CPU 采样不完整时为 N/A。 本机有 14 个逻辑核心，整机占比 = 进程 CPU% / 14，单核 100% 对应整机 7.14%。

- Source SHA-256: `fa69d6ce97745281290a263cb0f2515470ede0edb77f5132ce6e3ac7ae7cd9b4`
- 测量数：2,376；有效配置：782；失败配置：10。

Apple Clang 21.0.0; CMake 4.3.3; Ninja; Release -O3 -DNDEBUG -std=gnu++20 -arch arm64; TLS ON; sanitizers OFF. Standalone Asio 1.38.2 with BHO Beast based on Boost 1.84 (API 351); Boost 1.90.0 with Asio 1.38.0 and Beast API 359; OpenSSL 3.6.4. Mac mini on AC power, low power mode disabled, no CPU affinity.

arknet 协议测试记录单消息 RTT；原生 Asio 的回调／协程匹配测试记录批次 RTT。后者的固定批次数据不能与前者直接组成协程加速比；公开协程端点 API 仍为 TODO。

<a id="tcp"></a>

## TCP

[图表、简短结论与失败记录](dimensions-tcp_CN.md)

<a id="udp"></a>

## UDP

[图表、简短结论与失败记录](dimensions-udp_CN.md)

<a id="websocket"></a>

## WebSocket

[图表、简短结论与失败记录](dimensions-websocket_CN.md)

<a id="tcps"></a>

## TCPS

[图表、简短结论与失败记录](dimensions-tcps_CN.md)

<a id="wss"></a>

## WSS

[图表、简短结论与失败记录](dimensions-wss_CN.md)

<a id="http"></a>

## HTTP

[图表、简短结论与失败记录](dimensions-http_CN.md)

<a id="https"></a>

## HTTPS

[图表、简短结论与失败记录](dimensions-https_CN.md)

<a id="native-tcp"></a>

## 原生 Asio TCP

[图表、简短结论与失败记录](dimensions-native-tcp_CN.md)

## 原始数据

[完整数值 JSON](dimensions-summary.json ':ignore')

- [standalone-callback.json](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-dimensions/standalone-callback.json): 756 次测量; executable SHA-256 `480122707b4b3ed592d1f580f5cf7092b8008ce52331f55f0e666e89c568be6b`
- [boost-callback.json](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-dimensions/boost-callback.json): 756 次测量; executable SHA-256 `7b2989fd23e15a68a662899d1906f3b094138018b8ada5904309b4a90d96ea34`
- [standalone-native.json](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-dimensions/standalone-native.json): 432 次测量; executable SHA-256 `5d7ac94153f942eda1494656edc865ab01817a7ea9122a240cc5f3260b061549`
- [boost-native.json](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-dimensions/boost-native.json): 432 次测量; executable SHA-256 `d68dc3aea70b8334e5fc88d3bda0739faeb8696842c4f2f582092d92cb1b2d6d`

[复现命令与指标定义](../testing_CN.md)

本机回环、固定连接分布和短时测量不代表生产容量，业务选型仍需按实际消息与连接分布复测。
