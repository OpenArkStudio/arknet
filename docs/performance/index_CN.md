# 性能

这些报告在 Mac mini M4 Pro 上测量本地回环负载。报告记录机器、源码、命令与
适用范围，数据不来自 CI。比较时应保持协议、包体、并发和执行方式一致。

下载[原始测量记录与校验工具](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-benchmark-comparison.tar.gz)及 [SHA256 校验和](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-benchmark-comparison.tar.gz.sha256)。数据作为 Release 附件提供，不写入源码仓库。

## 版本与执行方式对照

- [数据库客户端](database-comparison_CN.md)：v0.2.0 与完整关闭等待实现的 MySQL、Redis 吞吐、响应时间和 CPU 对照。
- [网络端点](transport-comparison_CN.md)：TCP、TLS TCP、HTTP、HTTPS、WebSocket 与 WSS 的相同负载对照。
- [5 秒负载对照](steady-workloads_CN.md)：8 组选定负载，每个版本使用五个独立进程，展示吞吐、CPU 和响应时间的变化。
- [Asio 回调与协程](asio-execution-control_CN.md)：同一版本、相同负载下的执行方式和线程数对照。

配置矩阵共 1,632 轮，5 秒负载对照共 80 轮。数据库使用原生连接回调，Asio 执行方式对照绕过 arknet 端点；不包含 ark 的数据库协程桥。

## RUDP 测量

[RUDP FEC 报告](rudp_CN.md)：两种 Asio 依赖方案共 96 组配置、276 次测量，覆盖
三种 IO 模型、仅 ARQ／固定／自适应策略、包体、在途请求与丢包条件。
SVG 图表对比吞吐量、p99 RTT、CPU、网络开销、恢复、重传与保留内存。
当前 RUDP 公共 API 使用回调，RUDP 协程仍为 TODO。

[RUDP 并发报告](rudp-concurrency_CN.md)：128／512／1024 个客户端、一个共享 io_context，比较 1 与 4 个端点线程；18 组、54 轮，包含吞吐量、CPU、p99 与进程峰值 RSS。

## 数据库客户端

- [MySQL](../features/mysql/performance_CN.md)：只读文本与预处理查询，比较包体、连接数与客户端排队深度；记录查询 RT、吞吐量、CPU 和内存。
- [Redis](../features/redis/performance_CN.md)：二进制 ECHO，比较流水线大小与在途批数；记录整批 RT、命令吞吐量、CPU 和内存。

两份报告均对照 Boost.Asio、Standalone Asio、明文 TCP 与验证证书的 TLS。
测量使用回调客户端，不包含连接池或数据库协程。

## UDP 测量

[UDP 性能报告](udp-update_CN.md)包含 504 次矩阵测量、
21 次缓冲探测，以及两种依赖方案各 60 秒的持续测试。

- [IO 模型](udp_CN.md)：单 context 单线程、单 context 多线程与多 context 分片。
- [包体与在途数量](dimensions-udp_CN.md)：各配置重复测量，保留失败组。
- [边界与丢失](udp-limits_CN.md)：数据报大小与接收队列溢出。

## 回调、协程与 CPU

[同负载原生 TCP 测量](dimensions-native-tcp_CN.md)使用相同批次负载比较回调与协程。
[IO 模型对比](io-comparison_CN.md)说明原始调度测量结果与比较范围。
这些程序使用原生 Asio，arknet 公共协程端点仍为 TODO。

## 协议结果

| 协议 | IO 模型结果 | 包体与批量结果 |
| --- | --- | --- |
| TCP | [结果](tcp_CN.md) | [结果](dimensions-tcp_CN.md) |
| TLS TCP | [结果](tcps_CN.md) | [结果](dimensions-tcps_CN.md) |
| WebSocket | [结果](websocket_CN.md) | [结果](dimensions-websocket_CN.md) |
| WSS | [结果](wss_CN.md) | [结果](dimensions-wss_CN.md) |
| HTTP | [结果](http_CN.md) | [结果](dimensions-http_CN.md) |
| HTTPS | [结果](https_CN.md) | [结果](dimensions-https_CN.md) |

[多协议汇总](overview_CN.md)与[维度汇总](dimensions_CN.md)展示各项负载的最终结果，
保留测量日期和源码哈希。
这些测量不代表 WAN 容量、IPv6 性能或生产环境的长期运行能力。

[测量方法与指标定义](methodology_CN.md) · [正确性验证](../project/testing_CN.md) · [缓冲配置](../guides/socket-buffers_CN.md)
