# 性能

这些报告在 Mac mini M4 Pro 上测量本地回环负载。报告记录机器、源码、命令与
适用范围，数据不来自 CI。比较时应保持协议、包体、并发和执行方式一致。

## RUDP 测量

[RUDP FEC 报告](rudp_CN.md)：两种 Asio 依赖方案共 96 组配置、276 次测量，覆盖
三种 IO 模型、仅 ARQ／固定／自适应策略、包体、在途请求与丢包条件。
SVG 图表对比吞吐量、p99 RTT、CPU、网络开销、恢复、重传与保留内存。
当前 RUDP 公共 API 使用回调，RUDP 协程仍为 TODO。

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
