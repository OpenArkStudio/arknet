# 性能

这些报告在 Mac mini M4 Pro 上测量本地回环负载。报告记录机器、源码、命令与
适用范围，数据不来自 CI。比较时应保持协议、包体、并发和执行方式一致。

## 当前 UDP 测量

[2026-10-12 复测](udp-update_CN.md)在来源端口修复后完成 504 次矩阵测量、
21 次缓冲探测，以及两种后端各 60 秒的持续测试。

- [IO 模型](udp_CN.md)：单 context 单线程、单 context 多线程与多 context 分片。
- [包体与在途数量](dimensions-udp_CN.md)：各配置重复测量，保留失败组。
- [边界与丢失](udp-limits_CN.md)：数据报大小与接收队列溢出。

## 回调、协程与 CPU

[同负载原生 TCP 测量](dimensions-native-tcp_CN.md)使用相同批次负载比较回调与协程。
[IO 模型对比](io-comparison_CN.md)说明原始调度测量结果与比较范围。
这些程序使用原生 Asio，arknet 公共协程端点仍为 TODO。

## 协议基线

| 协议 | IO 模型结果 | 包体与批量结果 |
| --- | --- | --- |
| TCP | [结果](tcp_CN.md) | [结果](dimensions-tcp_CN.md) |
| TLS TCP | [结果](tcps_CN.md) | [结果](dimensions-tcps_CN.md) |
| WebSocket | [结果](websocket_CN.md) | [结果](dimensions-websocket_CN.md) |
| WSS | [结果](wss_CN.md) | [结果](dimensions-wss_CN.md) |
| HTTP | [结果](http_CN.md) | [结果](dimensions-http_CN.md) |
| HTTPS | [结果](https_CN.md) | [结果](dimensions-https_CN.md) |

[2026-10-10 多协议汇总](overview_CN.md)与[维度汇总](dimensions_CN.md)是历史基线。
其中 UDP 数据早于来源端口修复，当前 UDP 结论以 2026-10-12 报告为准。
这些测量不代表 WAN 容量、IPv6 性能或生产环境的长期运行能力。

[测量方法与指标定义](methodology_CN.md) · [正确性验证](../project/testing_CN.md) · [缓冲配置](../guides/socket-buffers_CN.md)
