# UDP 客户端端点回归记录

## 失败证据

[CI 运行 38049776409](https://github.com/OpenArkStudio/arknet/actions/runs/38049776409)
只有 `benchmark_udp_sharded4` 失败：16 个客户端、1024 字节包体、每个客户端
1 条在途请求、4 个 context 各 1 个线程。正式测量出现 1 次内容错误、1 条未收到的
回包和排空超时，预热也失败。原日志没有记录本地端口。

[Linux 复现](https://github.com/OpenArkStudio/arknet/actions/runs/38193718991)使用原实现，
增加端点和内容诊断，将客户端数提高到 256，以增加端口冲突的出现概率。结果如下：

| 共用的本地端点 | 发出请求的客户端 | 收到其回包的客户端 |
| --- | ---: | ---: |
| `127.0.0.1:56562` | 65 | 127 |
| `127.0.0.1:49583` | 73 | 247 |

两对客户端均开启了 `SO_REUSEADDR`，预热和正式测量都出现客户端编号不匹配的
回包。正式测量记录了 2 次内容错误、2 条未收到的回包和 1 次排空超时，复现了
原失败的特征。由于原日志缺少端口信息，无法事后直接验证原来那次 16 客户端运行的端口冲突。

诊断脚本打印上述证据后，还因 runner 未安装 `rg` 而失败。这次运行用于保存
复现证据，不计为通过的功能检查。
原始输出保存在 [linux-before.log](linux-before.log)。

只修改默认复用选项后，同一个诊断程序在 Linux 上连续通过 **50 次、每次 256 个
客户端**的运行，没有重复端点、内容错误或未收到的回包：
[运行 38194451308](https://github.com/OpenArkStudio/arknet/actions/runs/38194451308)、
[原始结果](linux-after.log)。

## 修改与覆盖

UDP 客户端不再默认开启 `SO_REUSEADDR`，仍可在 `bind_init` 中显式设置。
基准程序在开始发送前检查客户端端点是否重复；内容、完成次数和丢失检查保持严格。

`tests/udp.cpp` 的 C++ 回归用例覆盖 IPv4 和 IPv6 的默认选项、相同本地与远端
端点组合的冲突，以及显式开启复用。TCP 客户端保留原有默认行为。

## 本机结果

测试日期为 2026-10-12。机器为 Mac mini `Mac16,11`、Apple M4 Pro、14 核、
48 GiB 内存、macOS 27.0.1（26A434）；工具链为 Apple Clang 21.0.0、CMake 4.3.3、
Asio 1.38.2、Boost 1.90.0、OpenSSL 3.6.4。源码基于
`8526ad840eef5a557e01267ccd5f6582602d8023`，加上包含本记录的提交中的 UDP 端点修复。

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| Standalone + TLS，Release，完整检查 | 41 项检查、134 个用例通过 | [日志](standalone-check.log) |
| Boost + TLS，Release，完整检查 | 41 项检查、134 个用例通过 | [日志](boost-check.log) |
| Standalone UDP，ASan/UBSan | 14 个用例通过 | [日志](udp-asan.log) |
| Standalone UDP，TSan | 14 个用例通过 | [日志](udp-tsan.log) |
| 原失败配置，Standalone | 30 个独立进程运行均通过 | [JSONL](standalone-repeated.jsonl) |
| 原失败配置，Boost | 30 个独立进程运行均通过 | [JSONL](boost-repeated.jsonl) |

60 次重复运行均成功完成预热，内容错误、未收到的回包和排空超时均为 0。
这些短测用于验证回归稳定性，其时间指标不作为发布的性能测量结果。

## 运行回归用例

```sh
build/tests/arknet_udp_test --test-case="UDP clients reserve exclusive*"
build/benchmarks/arknet_loopback_benchmark --protocol udp --payload 1024 \
  --clients 16 --window 1 --seconds 0.2 --warmup 0.1 \
  --io-model sharded --io-threads 4
```
