# 测试与测量方法

[本地性能报告](performance/overview_CN.md)

功能测试验证正确性，持续压力测试检查重复工作、重启与清理。
性能测量在这些检查通过后于本机执行，使用不带 Sanitizer 的 Release 构建。
CI 用于回归和冒烟执行，共享 runner 的时间不作为发布的性能基线。

## 构建与执行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
cmake --build build --target arknet_check
build/tests/arknet_stress_test --protocol all --seconds 60 \
  --clients 16 --rounds 2 --window 4 --certs tests/certs
```

使用 Boost 时，将 Asio 头文件路径参数替换为
`-DARKNET_USE_BOOST_ASIO=ON`。Windows 可执行文件带 `.exe` 后缀。
仓库提供的 TLS 证书仅用于测试，不用于生产身份。

测试使用 doctest 2.5.3，源文件保存在 `tests/vendor/doctest`，
不会安装，也不成为应用依赖。`arknet_check` 直接执行测试程序、
安装包消费项目和已启用的基准冒烟用例，设置超时，并将日志写入
`build/tests/logs/`，不使用 CTest。

```sh
build/tests/arknet_tcp_test --list-test-cases
build/tests/arknet_tcp_test --test-case="*framing*"
build/tests/arknet_tls_test --reporters=junit --out=build/tls.xml
```

功能测试和性能基准都是包含独立 `main` 的 C++ 程序，编译后直接运行。
这些 C++ 功能测试和单项基准不需要 Python、Ruby、`jq`、GNU `timeout` 或 `shasum`。

### 单项基准

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
    > "build/tcp-${repetition}.json" || exit 1
done
```

## 正确性覆盖

| 测试组 | 内容 |
| --- | --- |
| TCP／UDP／WebSocket | 拆包、二进制／空数据、自定义 Session、端点路由、重连和发送所有权 |
| HTTP／router | 独立 raw 对端的 chunked／HEAD／Expect、有序流水线、上限、方法／路径、中间件、错误和 HTTP/HTTPS 绑定 |
| lifecycle／detail／IO pool | timer／post 取消、实际完成后销毁、mutable 监听器、执行器串行化、排队任务和重启 |
| concurrency／keepalive | 发送配额、回调完成计数、并发停止和 TCP keepalive |
| TLS | 信任／身份拒绝、mTLS、截止时间、WSS 和重连时创建新 stream |
| IPv6 | TCP、UDP/session/cast、WS/WSS、HTTP/HTTPS、TLS IP 身份验证和握手 Host |
| 安装包消费项目 | 后端／TLS 配置传播与已安装公开头文件 |

持续压力测试验证数据与序号、完成计数、连接／服务端重启、未完成写入和队列清理。
每种协议将 `--seconds` 分配给四种配置：库内置调度器、单 context 单线程、
单 context 四线程、四个 context 各一线程，再分给指定重启轮次。因此七种协议使用 `--seconds 60`
时，请求的流量阶段共七分钟，另加启动和收尾。慢接收者用例测试背压，不用于比较吞吐量。

## 本机验证

本轮机器为 Mac mini `Mac16,11`、Apple M4 Pro、14 个物理／逻辑核心（10 个性能核心、4 个能效核心）、
48 GiB 内存、macOS 27.0.1（26A434）、Apple Clang 21.0.0、CMake 4.3.3、
standalone Asio 1.38.2、Boost 1.90.0、OpenSSL 3.6.4。
2026-10-10 的完整检查结果如下。常规构建包含基准冒烟和安装包消费项目；
Sanitizer 构建关闭性能基准，避免混入测量。检查数是 `arknet_check` 执行的
程序数，用例数是 doctest 用例总数。

| 后端 | 构建 | TLS | 检查 / 用例 | 结果与日志 |
| --- | --- | --- | ---: | --- |
| standalone | Release | ON | 40 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone.log) |
| standalone | Release | OFF | 30 / 87 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-notls.log) |
| standalone | Debug | ON | 40 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-debug-tls.log) |
| standalone | Debug | OFF | 30 / 87 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-debug-notls.log) |
| Boost | Release | ON | 40 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost.log) |
| Boost | Release | OFF | 30 / 87 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-notls.log) |
| Boost | Debug | ON | 40 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-debug-tls.log) |
| Boost | Debug | OFF | 30 / 87 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-debug-notls.log) |
| standalone | Debug + ASan/UBSan | ON | 15 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-asan.log) |
| standalone | Debug + TSan | ON | 15 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-tsan.log) |

加入 IPv6 后，两种后端的 Release + TLS 完整检查再次通过，各 **41 个检查、128 个用例**：
[standalone 日志](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/final-standalone-tls.log)、
[Boost 日志](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/final-boost-tls.log)。
IPv6 独立检查覆盖两后端及 TLS 开关；ASan/UBSan、TSan 和 30 次短基准探测也通过，
详见 [IPv6 测试记录](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/ipv6/README_CN.md)。
这些短探测验证参数、协议和 CPU 百分比输出，不作为性能比较。

修复 future 回调释放、MSVC 拆包条件、UDP 完成字节数和排队取消的循环引用，
并统一 clang-format 18 格式后，完整本地检查再次通过：
两种 Release 后端各 **41 个检查、133 个用例**，standalone 的两种 Sanitizer 构建各
**16 个检查、133 个用例**。GCC/libstdc++ 回归检查也通过，详见
[可移植性测试记录](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/portability/README_CN.md)。

关闭流程的回归用例覆盖跨 context 保留的 Session/strand，以及协程连接后的异常
清理。历史诊断数据保存在 `benchmarks/results/diagnostics/`；发布的性能报告
只使用关闭修复后重新测量的完整数据。

本机具备 ASan/UBSan 和 TSan，单独执行，不混入性能测量。
LeakSanitizer 的支持依平台而异；Apple ASan 通过不等同于 Linux 泄漏检查通过。

### Sanitizer 检查

Sanitizer 在开发构建中插入检测代码，报告测试运行时发现的错误。

| 工具 | 检查范围 |
| --- | --- |
| ASan（AddressSanitizer） | 内存错误，例如越界访问、释放后继续访问和重复释放 |
| TSan（ThreadSanitizer） | 未正确同步的并发内存访问，且至少一次访问是写入 |
| UBSan（UndefinedBehaviorSanitizer） | 部分未定义行为，例如有符号整数溢出、无效移位和未对齐访问 |

ASan/UBSan 与 TSan 使用独立构建。它们会增加运行开销，发布的性能测量关闭这些
工具。测试通过只覆盖本次执行的代码路径和线程调度，不能证明所有错误都不存在。

## 性能矩阵

原有 IO 拓扑基线与扩展负载矩阵是独立数据集。
[包体、批量与执行方式对比](performance/dimensions_CN.md)固定连接数，分别比较其他维度。

### 包体、批量与执行方式

维护者可用 `bash scripts/benchmark_matrix.sh` 收集完整矩阵。
只有批量工具需要 `jq`、GNU `timeout`（macOS 可用 `gtimeout`）、`shasum` 和常用
shell 工具。它直接启动相同的 C++ 程序，保留执行命令、JSON、失败信息、机器信息和哈希。

```sh
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_loopback_benchmark \
  --backend standalone --certs tests/certs --clients 16 --windows 1 4 16 64 \
  --payloads 64 1024 16384 --io-models shared sharded --io-threads 1 4 \
  --seconds 1 --warmup 0.25 --repetitions 3 --keep-going \
  --output benchmarks/results/local-dimensions-standalone-callback.json
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_coroutine_benchmark \
  --backend standalone --certs tests/certs --protocols tcp --clients 16 \
  --windows 1 4 16 64 --payloads 64 1024 16384 \
  --io-models shared sharded --io-threads 1 4 --work-values 0 10000 \
  --execution-modes callback coroutine --seconds 1 --warmup 0.25 --repetitions 3 \
  --output benchmarks/results/local-dimensions-standalone-native.json
```

再用 Boost 构建目录中的两个可执行文件运行相同命令，将 `--backend` 改为 `boost`，
输出分别设为 `local-dimensions-boost-callback.json` 和 `local-dimensions-boost-native.json`。
每种后端包含 **756 次 arknet 回调测量和
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
测量完成后，可按[指标与图表](#指标与图表)创建离线报告生成环境，再使用四个数据文件生成对比：

```sh
build/report-venv/bin/python benchmarks/dimensions.py \
  --reports benchmarks/results/local-dimensions-standalone-callback.json \
            benchmarks/results/local-dimensions-boost-callback.json \
            benchmarks/results/local-dimensions-standalone-native.json \
            benchmarks/results/local-dimensions-boost-native.json \
  --output-dir docs/performance
```

### 原有 IO 拓扑基线

```sh
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_loopback_benchmark \
  --certs tests/certs --seconds 1 --warmup 0.25 --repetitions 3 \
  --output benchmarks/results/local-loopback.json
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_coroutine_benchmark \
  --certs tests/certs --protocols tcp --work-values 0 10000 \
  --seconds 1 --warmup 0.25 --repetitions 3 \
  --output benchmarks/results/local-coroutine.json
```

每种后端的回调矩阵包含 **630 次测量**：
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
[IO 模型与协程](threading_CN.md)说明业务选型。

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
[IPv4 与 IPv6](addressing_CN.md)。

UDP 请求 64 KiB 发送缓冲和 4 MiB 接收缓冲，JSON 记录服务端实际值和客户端最小／最大值。
操作系统可能限制请求。默认 UDP 窗口不模拟不可靠网络。
本机回环接口 `lo0` 的 MTU 为 16384 字节。

Python 和 Matplotlib 是可选的离线报告工具，不参与 C++ 网络测试的编译、启动或测量。
测量结束后，可将同机器、同源码哈希和相同测量时长的完整数据转成图表与中英文表格：

```sh
python3 -m venv build/report-venv
build/report-venv/bin/pip install -r benchmarks/requirements-report.txt
build/report-venv/bin/python benchmarks/report.py \
  --reports benchmarks/results/local-loopback.json benchmarks/results/local-coroutine.json \
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

## CI 与未覆盖范围

CI 在 Linux／macOS／Windows 检查两种后端，Linux 另运行 ASan/UBSan 和 TSan。
Job 名称包含系统、后端、TLS 和检查模式。main push 与 PR 使用独立触发，
过期运行会取消。CI 上传测试日志、执行基准冒烟，不生成性能报告矩阵。

本机回环不能预测跨主机容量和广域网延迟。丢包／延迟注入、文件描述符耗尽
和生产时长耐久仍需单独验证。`benchmarks/results/macos-arm64-*` 是重写前的历史
证据，不能代表当前实现的性能。

文档中的 14 个独立客户端、服务端程序也在两种后端上编译、运行通过。每种后端
的七组协议检查了回显内容，全部 28 个进程正常退出。
[构建、运行日志与复现脚本](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/usage/README_CN.md)
独立保留这项检查，不混入性能测量。
