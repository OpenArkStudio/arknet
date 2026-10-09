# 原生批量流控诊断

[English](README.md) | [中文](README_CN.md)

`arknet-native-max-batch-probes.log` 保留了 2026-10-10 的修复前故障：standalone
callback 执行方式、一个 io_context/四个线程、4 个客户端、65,507 字节消息、
每批 64 条消息、预热 0.2 秒、测量 0.3 秒。进程返回 1，错误为
`native TCP traffic timed out`。

客户端发送完整批量后才读取回复，服务端则逐条读取并回复；双方发送缓冲区可能
同时填满，导致双方等待对端读取。两种执行方式均已调整为完整读取批量、逐条
处理固定大小的消息，再回复完整批量。

此文件是历史故障证据，不能作为成功测量结果。[修复后验证](../../../../tests/results/macmini-m4pro-20261010/native/README_CN.md)
的 136 组参数探测全部通过；standalone Release、Boost Release、ASan/UBSan、
TSan 各通过 3 个永久测试用例、20 个断言。最大批量已由永久测试和 8 组修复后
探测覆盖。
