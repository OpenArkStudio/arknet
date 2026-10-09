# 原生 TCP 验证

[English](README.md) | [中文](README_CN.md)

本轮于 2026-10-10 在 Mac mini M4 Pro 上验证。日志覆盖批量流控修复后的
原生 Asio callback/coroutine 基准程序。

| 配置 | 测试用例 | 断言 | 结果 |
| --- | ---: | ---: | --- |
| Standalone Asio 1.38.2，Release | 3 | 20 | 通过 |
| Boost 1.90.0，Release | 3 | 20 | 通过 |
| Standalone，Debug，ASan/UBSan | 3 | 20 | 通过 |
| Standalone，Debug，TSan | 3 | 20 | 通过 |

永久测试覆盖注入异常后的取消与关闭、窗口为 1/64 时的批量内容校验，以及
65,507 字节载荷、每批 64 条消息的最大参数。两种执行方式和两种多线程
io_context 拓扑均已覆盖。

`arknet-native-final-probes.log` 记录了两个后端、两种执行方式共 136 组通过的检查：

- 96 组：64 字节消息、4 个客户端、窗口 1/4/16/64、每条消息 0/10,000 次 CPU
  迭代，分别使用一个 io_context/一个线程、一个 io_context/四个线程、
  四个 io_context/四个线程。
- 32 组：16 个客户端、16,384 字节消息与窗口 64，或 64 字节消息与窗口 4，
  两种 CPU 负载，以及两种四线程拓扑。
- 8 组：65,507 字节消息、窗口 64、4 个客户端，以及两种四线程拓扑。

每组检查执行方式与后端元数据、内容与 IO 错误计数、消息数 = 批量观测数 × 窗口、
RTT 样本数与分位数顺序、有效预热流量、持续时间和进程 CPU 时间。窗口为 1 时
记录单消息 RTT，其他窗口记录批量 RTT。这些检查用于功能验证；并发构建或测试
可能影响其耗时，不能作为正式性能测量。

两种服务端均完整读取并回复一批消息，CPU 模拟负载仍按固定载荷大小逐条执行。
修复前的双方发送等待保存在[独立诊断目录](../../../../benchmarks/results/diagnostics/native-batch-flow-control/README_CN.md)。

`*-build.log` 与 `*-test.log` 保留四组构建和测试结果。Sanitizer 均使用
`halt_on_error=1`，未报告 sanitizer 错误。验证时源码的 SHA256：

```text
fe4b079850125820f88cc58c74f1bc63d3cdeab6415513f44475dc7d16eda565  benchmarks/coroutine.cpp
0158fdb3c9d6665a2c4d9fa4b03b75ec5ff26ae474a16222f19113bb70aa2713  tests/coroutine_shutdown.cpp
```
