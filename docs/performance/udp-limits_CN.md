# UDP 报文大小与突发丢包

测量日期为 2026-10-12。[测试机器与接收缓冲诊断](udp-update_CN.md)。

Mac mini M4 Pro Release IPv4 回环：16 客户端、1 context、1 线程，无额外计算；测量 1 秒、预热 0.25 秒。每种依赖六种包体 × 三档窗口 × 三次：**36 次通过、18 次失败**。

![不同包体和请求窗口下的 IPv4 UDP 丢失回显数](assets/udp-limits-loss.svg)

图示测量阶段每次丢失回显数：三次重复、两种依赖的计数均相同。预热错误保留在 JSON；失败配置不参与吞吐量比较。

- 所有包体在窗口 1 下都通过，包括 **65,507 字节**；1,472 字节在窗口 16 和 64 下也通过。
- 窗口 64 下，8,192 字节每次丢失 512 条回显；16,356／16,357／16,384 字节各丢失 767 条。
  65,507 字节在窗口 16 下丢失 191 条，在窗口 64 下丢失 959 条。

## 包体与 MTU 上限

| 普通 UDP 包体 | IPv4 | IPv6 |
| --- | ---: | ---: |
| 协议最大值 | 65,507 = 65,535 − 20 − 8 | 65,527 = 65,535 − 8 |
| MTU 1,500 时的免分片上限 | 1,472 = 1,500 − 20 − 8 | 1,452 = 1,500 − 40 − 8 |
| MTU 16,384 时的数学预算 | 16,356 | 16,336 |

<Aside type="caution" title="MTU 与验证范围">

预算按 IPv4 头 20 字节、IPv6 基础头 40 字节、UDP 头 8 字节计算；IP options／扩展头会减小包体，不含 IPv6 jumbogram。IPv6 路由器不分片；本诊断仅测 IPv4，不证明 IPv6 负载上限。

`lo0` MTU 为 16,384，更大报文成功时 IPv4 分片／重组计数仍为零；数学边界不是实测分片阈值。跨主机应按实际路径 MTU 选包体。

</Aside>

## 内核证据

| 依赖方案 | 接收缓冲溢出计数：开始 → 结束 | 增量 | IPv4 分片／重组增量 |
| --- | ---: | ---: | ---: |
| Standalone Asio | 89,059 → 112,837 | 23,778 | 0 / 0 |
| Boost.Asio | 126,672 → 150,450 | 23,778 | 0 / 0 |

每种依赖测量阶段丢失 11,889 条回显。系统全局内核计数含预热，记录了突发接收缓冲溢出；没有 IPv4 分片计数支持「分片导致丢包」。发送拒绝、发送完成与内容错误均为零。

实际 socket 缓冲为 **64 KiB 发送／4 MiB 接收**；`net.inet.udp.maxdgram=9216`、`net.inet.udp.recvspace=786896` 未变。65,507 字节成功回显说明 9,216 不是这些 socket 的有效包体上限。16 KiB 在窗口 16 通过、窗口 64 溢出：报文有效性与突发容量应分别判断。

## 复现

<Steps>

<Step title="运行独立用例">

按 [UDP 构建说明](../features/udp/performance_CN.md#%E6%9E%84%E5%BB%BA%E4%B8%8E%E6%89%A7%E8%A1%8C)编译后，直接运行独立 C++ 用例：

```sh
build-perf/benchmarks/arknet_loopback_benchmark \
  --protocol udp --payload 65507 --clients 16 --window 1 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
```

</Step>

<Step title="读取归档并生成图表">

归档的[Standalone 命令与内核快照](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results/macmini-m4pro-20261012-udp/limits)
也保留了完整 shell 矩阵和 Boost 命令。可选离线图表工具使用[报告环境](methodology_CN.md#%E6%8C%87%E6%A0%87%E4%B8%8E%E5%9B%BE%E8%A1%A8)：

```sh
build/report-venv/bin/python scripts/udp_limits_report.py \
  --input-dir /path/to/arknet-history/benchmarks/results/macmini-m4pro-20261012-udp/limits \
  --output docs/performance/assets/udp-limits-loss.svg
```

</Step>

<Step title="验证离线工具">

报告工具检查使用确定性的合成矩阵验证计算与异常输入，不读取历史测量或启动网络测试：

```sh
python3 -m unittest discover -s scripts -p 'test_udp_limits_report.py' -v
```

[Standalone 原始 JSON](https://raw.githubusercontent.com/OpenArkStudio/arknet/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results/macmini-m4pro-20261012-udp/limits/standalone.json) ·
[Boost 原始 JSON](https://raw.githubusercontent.com/OpenArkStudio/arknet/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results/macmini-m4pro-20261012-udp/limits/boost.json) ·
[图表数值](assets/udp-limits-loss.json ':ignore') · [环境与测量定义](methodology_CN.md)

源码 SHA-256：`0771574af8ca56f1914deb544b956079e0a2f24021953ded5f5d524e3df334b1`。

这些突发为什么会溢出、如何设置缓冲，见[缓冲区与流量控制](../guides/socket-buffers_CN.md)。

</Step>

</Steps>
