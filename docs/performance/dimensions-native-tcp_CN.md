# 原生 Asio TCP：回调与协程 负载对比

16 个客户端；消息为 64/1024/16384 字节，窗口为 1/4/16/64；每个配置重复三次。

回调与协程都使用原生 Asio 和固定长度 TCP 批次：服务端完整读取一批，逐条执行相同计算后整批回写。窗口为 1 时记录单消息 RTT，更大窗口记录批次 RTT。arknet 公开协程端点 API 仍为 TODO。

平均进程 CPU 占用率以单核为 100%，多线程可超过 100%；旧样本标为估算，三次 CPU 采样不完整时为 N/A。 本机有 14 个逻辑核心，整机占比 = 进程 CPU% / 14，单核 100% 对应整机 7.14%。

## 无额外业务计算

![往返吞吐量](assets/dimensions-native-tcp-iterations0-throughput.svg)

![双向应用负载吞吐量](assets/dimensions-native-tcp-iterations0-payload.svg)

![批次 p99 RTT](assets/dimensions-native-tcp-iterations0-latency.svg)

![平均进程 CPU 占用率](assets/dimensions-native-tcp-iterations0-cpu.svg)

![协程／回调吞吐倍率](assets/dimensions-native-tcp-iterations0-ratio.svg)

![协程／回调 CPU 占用倍率](assets/dimensions-native-tcp-iterations0-cpu-ratio.svg)

- Standalone Asio: 匹配配置的协程／回调吞吐倍率中位数范围为 0.92–1.08。
- Standalone Asio: 匹配配置的协程／回调 CPU 占用倍率中位数范围为 0.98–1.02；小于 1 表示更低的平均进程 CPU 占用，需结合吞吐倍率判断。
- Boost.Asio: 匹配配置的协程／回调吞吐倍率中位数范围为 0.92–1.11。
- Boost.Asio: 匹配配置的协程／回调 CPU 占用倍率中位数范围为 0.92–1.05；小于 1 表示更低的平均进程 CPU 占用，需结合吞吐倍率判断。

## 每次回显增加 10,000 次确定性运算

![往返吞吐量](assets/dimensions-native-tcp-iterations10000-throughput.svg)

![双向应用负载吞吐量](assets/dimensions-native-tcp-iterations10000-payload.svg)

![批次 p99 RTT](assets/dimensions-native-tcp-iterations10000-latency.svg)

![平均进程 CPU 占用率](assets/dimensions-native-tcp-iterations10000-cpu.svg)

![协程／回调吞吐倍率](assets/dimensions-native-tcp-iterations10000-ratio.svg)

![协程／回调 CPU 占用倍率](assets/dimensions-native-tcp-iterations10000-cpu-ratio.svg)

- Standalone Asio: 匹配配置的协程／回调吞吐倍率中位数范围为 0.95–1.01。
- Standalone Asio: 匹配配置的协程／回调 CPU 占用倍率中位数范围为 0.96–1.01；小于 1 表示更低的平均进程 CPU 占用，需结合吞吐倍率判断。
- Boost.Asio: 匹配配置的协程／回调吞吐倍率中位数范围为 0.93–1.02。
- Boost.Asio: 匹配配置的协程／回调 CPU 占用倍率中位数范围为 0.95–1.01；小于 1 表示更低的平均进程 CPU 占用，需结合吞吐倍率判断。

[完整数值 JSON（含 p95、运行范围与失败记录）](dimensions-summary.json ':ignore')

## 失败配置

本协议没有失败配置。

任一次重复失败，该配置的三次测量均不进入吞吐量、延迟或匹配倍率统计；原始数据保留失败记录。

吞吐和 CPU 倍率按相同后端、消息、批次大小、线程模型和计算量逐次配对后取中位数；1 表示相同。CPU 占用更低不一定处理更多请求，需结合吞吐倍率判断。CPU 缺少任一次采样时，CPU 统计和对应倍率为 N/A，吞吐统计仍可保留。该结果不能推算 arknet 回调端点与未来协程 API 的速度差异。

[指标、统计口径与复现](../testing_CN.md)
