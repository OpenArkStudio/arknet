# RUDP 与 FEC 验证

[English](README.md) | [中文](README_CN.md)

验证日期为 2026-10-13。机器为 Mac mini `Mac16,11`、Apple M4 Pro、14 个核心
（10 个性能核心、4 个能效核心）、48 GiB 内存、macOS 27.0.1（26A434）、
Apple Clang 21.0.0、CMake 4.3.3、Standalone Asio 1.38.2、Boost 1.90.0、OpenSSL 3.6.4。
源码为已发布基线 `15ff563` 加包含本记录的提交。测量源码清单和二进制哈希保存在
[性能数据](../../../../benchmarks/results/macmini-m4pro-20261013/rudp-fec/metadata.json)中。
RUDP/FEC 不包含在 `v0.1.0-rc.1` 中。

## 结果

| 检查 | 范围 | 结果 |
| --- | --- | --- |
| Standalone Asio，Release + TLS | 55 项检查／219 个 doctest 用例 | [通过](arknet-rudp-fec-check-standalone.log) |
| Boost.Asio，Release + TLS | 55 项检查／219 个 doctest 用例 | [通过](arknet-rudp-fec-check-boost.log) |
| ASan/UBSan，Standalone Asio | 10 个指定测试组／109 个用例 | [日志](arknet-rudp-final-asan/) |
| TSan，Standalone Asio | 8 个指定测试组／86 个用例 | [日志](arknet-rudp-final-tsan/) |
| 真实 zephyr/arknet 对端，每种后端 | 12 组进程／16 个会话／512 条消息及回显 | [证据](interop/arknet-zephyr-interop-final.md) |
| 格式与排除规则 | 146 个项目 C++ 文件；排除第三方实现 | [检查](arknet-rudp-fec-format.log)、[脚本](arknet-rudp-fec-format-runner.log) |
| 报告生成器 | 数据结构与计数、SVG 链接、XML 和渲染 | [日志](arknet-rudp-report-verification.log) |
| 中英文文档 | 构建、路由、本地链接与 AI Markdown 导出 | [日志](arknet-rudp-fec-docs-check.log) |

完整检查包含安装包消费项目和基准冒烟用例；冒烟计时不作为性能结果。
RUDP FEC 在两种 Release 后端及两种 Sanitizer 构建中均通过 11 个用例、2,564 项断言。
ASan/UBSan 覆盖 ARQ、独立 ARQ/FEC 实现互通、RUDP/FEC、lifecycle、detail 与 concurrency；
TSan 覆盖 FEC、独立 FEC 实现互通、RUDP/FEC、lifecycle、detail 与 concurrency。
本记录不代表本机执行了全部历史测试组的 Sanitizer 检查。

网络测量完成 96 组配置、276 次测量和 7,960,439 次消息往返，交付、顺序、发送完成、
收尾及无效 FEC 检查错误均为零，包含六组 120 秒持续运行。
性能测量与 Sanitizer 分开执行。数值见 [RUDP 报告](../../../../docs/performance/rudp_CN.md)。

## 复现

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --target arknet_check --parallel 3
build/tests/arknet_fec_test
build/tests/arknet_fec_interop_test
build/tests/arknet_rudp_stability_test
build/tests/arknet_rudp_fec_test
```

使用 Boost.Asio 时，在独立构建目录将 Asio 路径替换为 `-DARKNET_USE_BOOST_ASIO=ON`。
可选真实对端测试需要提供旧库目录：

```sh
cmake -S . -B build-interop -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include \
  -DARKNET_ZEPHYR_ROOT=/path/to/zephyr
cmake --build build-interop --target arknet_zephyr_interop_check --parallel 3
```

[互通说明](../../../interop/README_CN.md)记录场景及仅在构建目录生成的旧库兼容文件。
旧库源码不会被修改或安装。互通测试使用仅 ARQ 会话，并保留 SYN/ACK/FIN；
未验证 FIN 丢失后的可靠关闭。

Sanitizer 使用独立 Debug 构建并关闭基准。ASan/UBSan：

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DARKNET_BUILD_BENCHMARKS=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build-asan --parallel 3
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  build-asan/tests/arknet_rudp_fec_test
```

本次 macOS 检查设置 `detect_leaks=0`，不代表通过了 Linux 泄漏检测。
仅测试用原版 KCP 源码关闭 `null,object-size` 检查，arknet 自有代码保留完整检测。
TSan 使用另一构建目录，将编译和可执行文件链接选项改为
`-fsanitize=thread -fno-omit-frame-pointer`，执行时设置
`TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`。
TSan 的 FEC 端点测试耗时 18.67 秒，内核 UDP 接收缓冲丢包计数未增加；
使用实际 1 MiB socket 缓冲及 16 个分片的 ARQ 发送窗口，保留并发发送与统计。

## 采用范围

这些检查验证了所执行的库行为及真实旧库互通。
ARK 业务接入、跨主机部署、文件描述符耗尽、生产时长耐久和部署身份验证仍需单独验收。
参见 [生产验收](../../../../docs/project/production-readiness_CN.md)。
