# 测试与验证

[本地性能报告](../performance/overview_CN.md)

[UDP 修复后复测与缓冲诊断](../performance/udp-update_CN.md) · [缓冲区配置](../guides/socket-buffers_CN.md)

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

## 正确性覆盖

| 测试组 | 内容 |
| --- | --- |
| TCP／UDP／WebSocket | 拆包、二进制／空数据、自定义 Session、端点路由、重连和发送所有权 |
| HTTP／router | 独立 raw 对端的 chunked／HEAD／Expect、有序流水线、上限、方法／路径、中间件、错误和 HTTP/HTTPS 绑定 |
| HTTP 请求 | 独立 raw 对端、HEAD／临时／chunked／EOF 响应、取消、截止时间、上限与完成回调恰好执行一次 |
| ARQ／RUDP | 原版协议互通、丢失／乱序、内存上限、握手重试、IPv4／IPv6、旧 SYN 处理和关闭 |
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
Standalone Asio 1.38.2、Boost 1.90.0、OpenSSL 3.6.4。

2026-10-12 加入 RUDP、ARQ 与 HTTP 单次请求后，两种 Release + TLS 后端
各通过 **48 项完整检查、174 个 doctest 用例**。新增模块的 ASan/UBSan
通过 40 个用例，RUDP／HTTP 请求的 TSan 通过 18 个用例。仅用于测试的原版
KCP 源码关闭 `null,object-size` 检测，arknet 自有代码保留完整检测。
[测试记录与复现命令](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261012/rudp/README_CN.md)
保存了日志；性能数据见独立的 [RUDP 报告](../performance/rudp_CN.md)。
新增能力仍为实验性，不包含在 `v0.1.0-rc.1` 中。

2026-10-10 的完整检查结果如下。常规构建包含基准冒烟和安装包消费项目；
Sanitizer 构建关闭性能基准，避免混入测量。检查数是 `arknet_check` 执行的
程序数，用例数是 doctest 用例总数。

| 后端 | 构建 | TLS | 检查 / 用例 | 结果与日志 |
| --- | --- | --- | ---: | --- |
| Standalone Asio | Release | ON | 40 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone.log) |
| Standalone Asio | Release | OFF | 30 / 87 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-notls.log) |
| Standalone Asio | Debug | ON | 40 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-debug-tls.log) |
| Standalone Asio | Debug | OFF | 30 / 87 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-standalone-debug-notls.log) |
| Boost | Release | ON | 40 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost.log) |
| Boost | Release | OFF | 30 / 87 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-notls.log) |
| Boost | Debug | ON | 40 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-debug-tls.log) |
| Boost | Debug | OFF | 30 / 87 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-boost-debug-notls.log) |
| Standalone Asio | Debug + ASan/UBSan | ON | 15 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-asan.log) |
| Standalone Asio | Debug + TSan | ON | 15 / 115 | [通过](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/arknet-runtime-matrix-tsan.log) |

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

2026-10-12 修正 UDP 客户端默认开启 `SO_REUSEADDR` 的行为后，两种 Release + TLS
后端各通过 **41 项检查、134 个用例**，并各用独立进程重复执行原失败的 UDP
配置 30 次，全部通过。UDP 的 ASan/UBSan 和 TSan 检查也通过。
[UDP 端点回归记录](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261012/udp-endpoints/README_CN.md)
保存了 Linux 端口冲突证据和本地日志。

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
