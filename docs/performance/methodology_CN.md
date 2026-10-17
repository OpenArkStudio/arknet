# 性能测量方法与指标

使用独立的 Release C++ 程序在本地测量。CI 只执行回归与 smoke 检查；公开报告记录机器、配置、原始数据和测量范围。

原始 JSON 和日志保存到源码仓库外。下文的 `/path/to/arknet-results` 需替换为已创建的外部目录。离线报告工具的 `--raw-base`（RUDP 工具为 `--raw-url`）可指定已发布归档；未指定时仅显示输入名称，不生成原始数据链接。

## 单项基准

```sh
build/benchmarks/arknet_loopback_benchmark \
  --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
build/benchmarks/arknet_coroutine_benchmark \
  --execution coroutine --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --work 0 --warmup 0.25 --seconds 1
```

每个程序向标准输出写入一条 JSON 结果，失败时返回非零退出码。
用简短 shell 循环为同一配置启动独立进程重复测量：

```sh
for repetition in 1 2 3; do
  build/benchmarks/arknet_loopback_benchmark \
    --protocol tcp --payload 1024 --clients 16 --window 16 \
    --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1 \
    > "/path/to/arknet-results/tcp-${repetition}.json" || exit 1
done
```

## 性能矩阵

原有 IO 拓扑基线与扩展负载矩阵是独立数据集。
[包体、批量与执行方式对比](dimensions_CN.md)固定连接数，分别比较其他维度。

### 包体、批量与执行方式

维护者可用 `bash scripts/benchmark_matrix.sh` 收集完整矩阵。
只有批量工具需要 `jq`、GNU `timeout`（macOS 可用 `gtimeout`）、`shasum` 和常用
shell 工具。它直接启动相同的 C++ 程序，保留执行命令、JSON、失败信息、机器信息和哈希。

```sh
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_loopback_benchmark \
  --backend standalone --certs tests/certs --clients 16 --windows 1 4 16 64 \
  --payloads 64 1024 16384 --io-models shared sharded --io-threads 1 4 \
  --seconds 1 --warmup 0.25 --repetitions 3 --keep-going \
  --output /path/to/arknet-results/local-dimensions-standalone-callback.json
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_coroutine_benchmark \
  --backend standalone --certs tests/certs --protocols tcp --clients 16 \
  --windows 1 4 16 64 --payloads 64 1024 16384 \
  --io-models shared sharded --io-threads 1 4 --work-values 0 10000 \
  --execution-modes callback coroutine --seconds 1 --warmup 0.25 --repetitions 3 \
  --output /path/to/arknet-results/local-dimensions-standalone-native.json
```

再用 Boost 构建目录中的两个可执行文件运行相同命令，将 `--backend` 改为 `boost`，
输出分别设为 `local-dimensions-boost-callback.json` 和 `local-dimensions-boost-native.json`。
每种依赖方案包含 **756 次 arknet 回调测量和
432 次原生 TCP 对照测量**。七种协议均使用显式设置的四档在途请求上限，
包括 UDP；批量大小与连接数分别设置。

原生测试使用相同的服务端整批读取与回显、客户端整批读写、包体检查、CPU 计算和
每连接 strand，对比 Asio 回调与 C++20 协程。它测量整批完成 RTT；每批只有
一个请求时等同于逐消息 RTT。arknet 的公共协程端点 API 仍为 TODO。
两种执行方式的 CPU 时间均包含预热与测量；服务端在回显前对批次中的每条消息
分别执行指定计算。

`--keep-going` 保留失败负载配置，完成请求的矩阵；只要有失败就返回非零退出码。
失败组展示错误和 UDP 丢包量，不参与有效吞吐量、RTT 对比。每组用独立进程
重复测量三次；原生测试在重复轮次之间交替执行顺序。流量采用闭环请求，RTT
包含排队时间，不测量独立定时到达的请求负载。
测量完成后，可按[指标与图表](#%E6%8C%87%E6%A0%87%E4%B8%8E%E5%9B%BE%E8%A1%A8)创建离线报告生成环境，再使用四个数据文件生成对比：

```sh
build/report-venv/bin/python benchmarks/dimensions.py \
  --reports /path/to/arknet-results/local-dimensions-standalone-callback.json \
            /path/to/arknet-results/local-dimensions-boost-callback.json \
            /path/to/arknet-results/local-dimensions-standalone-native.json \
            /path/to/arknet-results/local-dimensions-boost-native.json \
  --output-dir docs/performance
```

### 原有 IO 拓扑基线

```sh
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_loopback_benchmark \
  --certs tests/certs --seconds 1 --warmup 0.25 --repetitions 3 \
  --output /path/to/arknet-results/local-loopback.json
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_coroutine_benchmark \
  --certs tests/certs --protocols tcp --work-values 0 10000 \
  --seconds 1 --warmup 0.25 --repetitions 3 \
  --output /path/to/arknet-results/local-coroutine.json
```

每种依赖方案的回调矩阵包含 **630 次测量**：
七种协议 × 三种消息大小 × 两档流量 × 五种拓扑 × 三次重复。
协程 TCP 矩阵包含 **180 次测量**，增加两档业务计算量。
运行脚本默认测量三秒、预热一秒；上面的命令与较短的本机基线一致。
关闭 TLS 时应指定 `--protocols tcp udp websocket http`。
这些命令在当前代码上复现基线负载参数。发布的基线记录了原始源码和程序哈希；
复现当时的实现应使用对应归档源码。

| 维度 | 参数 |
| --- | --- |
| 协议 | 使用 `use_dgram` 的 TCP、UDP、WebSocket、TCPS、WSS、HTTP、HTTPS |
| 消息大小 | 64、1024、16384 字节 |
| 流量 | 1 个客户端／window 1；16 个客户端／window 16（UDP 为1） |
| IO | 一个 context，分别由 1、2、4 个线程运行；2 或 4 个 context，各由一个线程运行 |
| 协程业务计算 | 服务端每次回显前执行0或10000次确定性 CPU 计算 |

一个 `io_context` 可以由一个或多个线程运行。多线程配置为每个连接使用独立
strand；分片配置为每个 `io_context` 使用一个运行线程。客户端和服务端共享
同一套 IO 调度资源。只有一个 context 时，两种配置相同，因此不重复测量。
[IO 模型与协程](../concepts/io-models_CN.md)说明业务选型。

每次测量启动独立进程，先预热，再测量，最后最多等待两秒收完未完成流量。
收集数据前停止并行构建和其他高负载任务。运行脚本保留失败前的部分结果；
数据／顺序错误、发送拒绝／完成错误、UDP 丢失、异常断连和未完成流量均使测量无效。

## 指标与图表

| 指标 | 定义 |
| --- | --- |
| 往返／s | 完成回显数除以测量墙钟时间 |
| Payload MiB/s | `2 × payload字节数 × 完成往返数 / 秒 / 1048576`，不含头部和加密 |
| RTT p50／p95／p99 | 从提交发送至匹配回显，包含排队和业务处理；使用采样 |
| 平均 CPU 占用率 | 进程 CPU 时间除以同一区间的墙钟时间，再乘 100%；单核 100%，多线程可超过 100% |
| Peak RSS | 整个进程的常驻内存峰值，不证明没有泄漏 |
| 错误 | 数据／顺序、发送完成／拒绝、丢失／未完成、异常与收尾超时 |

CPU 包含同一进程中客户端、服务端的全部线程，不代表功耗。本机有 14 个逻辑核心，
进程 400% 相当于整机计算容量的约 28.6%。新程序记录 `cpu_elapsed_seconds`；
历史结果未记录独立的 CPU 采样墙钟区间，报告标为估算：协议测试用测量与排空时长，
原生回调／协程对照用预热加测量时长作分母。

两个基准程序及批量脚本支持 `--address-family ipv6`，通过 `::1` 测试；
默认 `ipv4` 使用 `127.0.0.1`。现有性能报告测量 IPv4，IPv6 的用法与验证范围见
[IPv4 与 IPv6](../guides/ipv4-ipv6_CN.md)。

UDP 请求 64 KiB 发送缓冲和 4 MiB 接收缓冲，JSON 记录服务端实际值和客户端最小／最大值。
操作系统可能限制请求。默认 UDP 窗口不模拟不可靠网络。
本机回环接口 `lo0` 的 MTU 为 16384 字节。

Python 和 Matplotlib 是可选的离线报告工具，不参与 C++ 网络测试的编译、启动或测量。
测量结束后，可将同机器、同源码哈希和相同测量时长的完整数据转成图表与中英文表格：

```sh
python3 -m venv build/report-venv
build/report-venv/bin/pip install -r benchmarks/requirements-report.txt
build/report-venv/bin/python benchmarks/report.py \
  --reports /path/to/arknet-results/local-loopback.json /path/to/arknet-results/local-coroutine.json \
  --output-dir docs/performance \
  --host-description "Actual hardware, compiler, dependencies and Release build flags"
```

Windows 使用 `build/report-venv/Scripts/python.exe`。
Matplotlib 只用于报告生成。生成器拒绝未完成、失败、重复或配置不兼容的数据。
图表显示重复测量的中位数和最小／最大值，误差线不是置信区间。
延迟是各次运行采样分位数的中位数，不是合并样本后的分位数。

报告工具维护者可以单独运行可选的 Python 检查：

```sh
build/report-venv/bin/python -m unittest discover -s benchmarks -p 'test_report.py' -v
build/report-venv/bin/python -m unittest discover -s benchmarks -p 'test_dimensions.py' -v
```

每连接允许多条在途请求时，协程测试程序测量**整批 RTT**，回调测试程序持续
补充请求并测量**逐消息 RTT**。只在同一测试程序内比较 IO 配置，不能据此
从这组原有基线推导协程相对回调的加速比。上面的原生对照测试为两种执行方式
使用相同测量方法。
