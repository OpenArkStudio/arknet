# 测试与验证

[本地性能报告](../performance/overview_CN.md)

[UDP 性能报告与缓冲诊断](../performance/udp-update_CN.md) · [缓冲区配置](../guides/socket-buffers_CN.md)

功能测试验证正确性，持续压力测试检查重复工作、重启与清理。
性能测量在这些检查通过后于本机执行，使用不带 Sanitizer 的 Release 构建。
CI 用于回归和冒烟执行，共享 runner 的时间不作为发布的性能基线。

## 构建与执行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_TESTS=ON \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_USE_BOOST_ASIO=ON
cmake --build build --parallel 3
cmake --build build --target arknet_check
build/tests/arknet_stress_test --protocol all --seconds 60 \
  --clients 16 --rounds 2 --window 4 --certs tests/certs
```

使用 Standalone Asio 时，在独立构建目录设置
`-DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`。
HTTP 和 WebSocket 检查依赖 Boost.Asio 与 Boost.Beast，因此两种配置的检查数量不同。
Windows 可执行文件带 `.exe` 后缀。
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
| FEC／RUDP FEC | 独立 Reed–Solomon 实现互通、协商、CRC 拒绝、重复与重放、恢复、不完整分组、内存预算、固定／自适应策略和并发统计 |
| lifecycle／detail／IO pool | timer／post 取消、实际完成后销毁、mutable 监听器、执行器串行化、排队任务和重启 |
| 进程退出／线程存储 | 回调释放最后一个所有者、嵌套线程池、直接及间接全局所有权、两种跨编译单元链接顺序、退出后拒绝构造及保留显式 Asio 配置 |
| concurrency／keepalive | 发送配额、回调完成计数、并发停止和 TCP keepalive |
| TLS | 信任／身份拒绝、mTLS、截止时间、WSS 和重连时创建新 stream |
| IPv6 | TCP、UDP/session/cast、WS/WSS、HTTP/HTTPS、TLS IP 身份验证和握手 Host |
| MySQL | 报文／认证／命令编解码、TLS 身份验证、查询、预处理、事务、结果上限、取消、有界准入，以及独占连接池的重置与数据库隔离 |
| Redis | RESP2/RESP3、认证、TCP/TLS/mTLS、有序流水线、取消后的回复排空、有界准入、拒绝复制命令，以及独占连接池的重置、认证与数据库隔离 |
| 安装包消费项目 | 依赖方案／TLS 配置传播与已安装公开头文件 |

持续压力测试验证数据与序号、完成计数、连接／服务端重启、未完成写入和队列清理。
每种协议将 `--seconds` 分配给四种配置：库内置调度器、单 context 单线程、
单 context 四线程、四个 context 各一线程，再分给指定重启轮次。因此七种协议使用 `--seconds 60`
时，请求的流量阶段共七分钟，另加启动和收尾。慢接收者用例测试背压，不用于比较吞吐量。

## 本机验证

验证日期为 2026-10-15。机器为 Mac mini `Mac16,11`、Apple M4 Pro、14 个物理／逻辑核心
（10 个性能核心、4 个能效核心）、48 GiB 内存、macOS 27.0.1（26A434）、
Apple Clang 21.0.0、CMake 4.3.3、Standalone Asio 1.38.2、Boost 1.90.0、OpenSSL 3.6.4。

| 依赖方案 | 构建／TLS | 检查数 | doctest 用例数 | 断言数 | 结果 |
| --- | --- | ---: | ---: | ---: | --- |
| Boost.Asio | Release／ON | 75 | 374 | 1,952,026 | 通过 |
| Standalone Asio | Release／ON | 62 | 314 | 976,980 | 通过 |

检查数是 `arknet_check` 执行的程序数，用例数是 doctest 用例总数。
两套完整检查均无失败或跳过的用例，并包含基准冒烟和安装包使用检查。
真实数据库测试使用隔离的 MySQL 8.4.11 与 Redis 7.4.2 服务；未提供服务环境变量时，
对应的可选用例会跳过，不会以模拟服务代替。

### 数据库客户端 Sanitizer 验证

独立构建使用 LLVM 22.1.7，测试框架和 arknet 代码均启用检测。

| 构建 | 范围 | 用例数 | 断言数 | 结果 |
| --- | --- | ---: | ---: | --- |
| Boost.Asio／TSan | MySQL connection 与 pool | 51 | 1,899 | 通过，0 跳过 |
| Standalone Asio／TSan | MySQL connection 与 pool | 51 | 1,899 | 通过，0 跳过 |
| Boost.Asio／ASan+UBSan | MySQL connection 与 pool | 51 | 1,899 | 通过，0 跳过 |
| Boost.Asio／TSan | Redis client 与 pool | 35 | 316 | 通过，0 跳过 |
| Standalone Asio／ASan+UBSan | Redis client 与 pool | 35 | 316 | 通过，0 跳过 |

Boost.Asio 数据库 TSan 执行合计 86 个用例、2,215 项断言。
MySQL 的 Standalone Asio TSan 与 Boost.Asio ASan+UBSan 分别覆盖相同的 51 个用例、1,899 项断言。
没有 Sanitizer 诊断，也没有使用抑制规则。macOS ASan 执行关闭了泄漏检测。
MySQL 包含投递返回前的运行时所有权，以及独立的无效连接参数／命令拒绝销毁用例；
Redis 包含 RESET 字节预算与 REPLCONF 回复关联回归。
[检查范围、环境与复现命令](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261015/database-clients/README_CN.md)
记录了各项区别。这些结果覆盖数据库测试组，不代表全项目都执行过 Sanitizer 检查。

MySQL 编解码测试包含 MSVC 断言诊断所需的标准流头文件。
命令聚合准入回归使用编码后合计超过预算的请求大小，不依赖字符串容量或小字符串优化（SSO）。

### RUDP 互通专项证据

以下专项执行记录于 2026-10-13，源码版本与范围保留在链接记录中；
其中较早的完整检查计数不代表当前项目。

| 检测工具 | 范围 | 用例 | 结果 |
| --- | --- | ---: | --- |
| ASan/UBSan | Standalone Asio：ARQ、独立 ARQ/FEC 实现互通、RUDP/FEC、lifecycle、detail 与 concurrency | 109 | 通过 |
| TSan | Standalone Asio：FEC、独立 FEC 实现互通、RUDP/FEC、lifecycle、detail 与 concurrency | 86 | 通过 |

RUDP FEC 端点测试在两种 Release 依赖方案及独立 ASan/UBSan、TSan 构建中均通过 11 个用例、
2,564 项断言。真实 zephyr/arknet 混合进程测试每种依赖方案均通过 12 组，覆盖重连、ARK 二进制
消息头、4096 字节消息体、ARQ 报文丢失和乱序。中继保留 SYN/ACK/FIN，未验证 FIN 丢失后的可靠关闭。

[日志与复现命令](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261013/rudp-fec/README_CN.md)
记录了源码版本和检查范围。仅用于测试的原版 KCP 源码关闭 `null,object-size` 检测，
arknet 自有代码保留完整检测。性能数据见独立的 [RUDP 报告](../performance/rudp_CN.md)。

<Aside type="note" title="部署容量">

本机正确性、Sanitizer 和回环网络结果不能证明可承载集群 10 万～20 万 PCU。
跨主机负载、资源预算、生产时长耐久以及 ARK 业务负载仍需单独验收。

</Aside>

Sanitizer 单独执行，不混入性能测量。LeakSanitizer 的支持依平台而异；
Apple ASan 通过不等同于 Linux 泄漏检查通过。

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

CI 在 Linux／macOS／Windows 检查两种依赖方案，Linux 另运行 ASan/UBSan 和 TSan。
Job 名称包含系统、依赖方案、TLS 和检查模式。main push 与 PR 使用独立触发，
过期运行会取消。CI 上传测试日志、执行基准冒烟，不生成性能报告矩阵。

本机 RUDP 测量覆盖随机／突发丢包、延迟、重复与乱序，并包含六组 120 秒持续运行。
这些结果不能预测跨主机容量和广域网延迟。文件描述符耗尽、生产时长耐久及
[ARK 业务验收](production-readiness_CN.md)仍需单独验证。

文档中的 14 个独立客户端、服务端程序也在两种依赖方案上编译、运行通过。每种依赖方案
的七组协议检查了回显内容，全部 28 个进程正常退出。
[构建、运行日志与复现脚本](https://github.com/OpenArkStudio/arknet/blob/main/tests/results/macmini-m4pro-20261010/usage/README_CN.md)
独立保留这项检查，不混入性能测量。
