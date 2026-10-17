# RUDP、ARQ 与 HTTP 请求验证

2026-10-12，Mac mini `Mac16,11`、Apple M4 Pro、14 个核心（10 个性能核心、
4 个能效核心）、48 GiB 内存、macOS 27.0.1（26A434）、Apple Clang 21.0.0、
CMake 4.3.3、Standalone Asio 1.38.2、Boost 1.90.0、OpenSSL 3.6.4。
源码为 `e1cdce5e58aa3f7d0376e8ccfda21448650c17a7` 加上包含本记录的提交。
这些新增能力不包含在 `v0.1.0-rc.1` 中。

## 结果

| 检查 | 用例 / 断言 | 证据 |
| --- | ---: | --- |
| Standalone Asio，Release + TLS，完整测试 | 48 项检查 / 174 个用例 | [日志](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/rudp/arknet-rudp-final-standalone.log) |
| Boost.Asio，Release + TLS，完整测试 | 48 项检查 / 174 个用例 | [日志](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/rudp/arknet-rudp-final-boost.log) |
| ARQ 核心，ASan/UBSan | 19 / 17,824 | [日志](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/rudp/arknet-arq-final-asan.log) |
| 原版 KCP 互通，ASan/UBSan | 3 / 21,571 | [日志](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/rudp/arknet-arq-interop-final-asan.log) |
| RUDP 网络，ASan/UBSan | 10 / 452 | [日志](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/rudp/arknet-rudp-final-asan.log) |
| HTTP 请求，ASan/UBSan | 8 / 70 | [日志](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/rudp/arknet-httprequest-final-asan.log) |
| RUDP 网络，TSan | 10 / 452 | [日志](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/rudp/arknet-rudp-tsan.log) |
| HTTP 请求，TSan | 8 / 70 | [日志](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/rudp/arknet-rudp-http-tsan.log) |

全部通过。完整测试包含安装包消费项目和三种 RUDP 基准冒烟配置，冒烟耗时
不作为性能结果。本轮 TSan 检查针对新增网络模块，不代表重新执行了历史
TSan 全套测试。

文档中的 RUDP 服务端／客户端、HTTP 单次请求与对应 HTTP 服务端共四个完整
程序也在两种依赖方案上编译、运行通过。两组程序返回了预期回显并正常停止；中英文
页面的 C++ 代码块一致。

ARQ 覆盖有序重组、丢失／乱序、有限重传、时间戳回绕、无效数据报、内存上限
和原版协议互通。RUDP 覆盖握手重试、消息分片、IPv4／IPv6、取消、关闭通知、
旧 SYN 拒绝和接收预算耗尽。HTTP 请求使用独立 raw 对端检查 HEAD、临时响应、
chunked／EOF 响应、上限、TRACE 校验、取消，以及完成回调恰好执行一次。

## 复现

构建并执行完整测试：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
cmake --build build --target arknet_check
```

使用 Boost.Asio 时，将 Asio 头文件路径参数替换为
`-DARKNET_USE_BOOST_ASIO=ON`。也可以直接运行独立 C++ 测试程序：

```sh
build/tests/arknet_arq_core_test
build/tests/arknet_arq_interop_test
build/tests/arknet_rudp_test
build/tests/arknet_httprequest_test
```

Sanitizer 使用独立构建，设置 `-DARKNET_BUILD_BENCHMARKS=OFF`、
`-DCMAKE_BUILD_TYPE=Debug` 和
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`，
运行时设置 `ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`。
TSan 使用另一个构建，编译参数为 `-fsanitize=thread -fno-omit-frame-pointer`，
运行时设置 `TSAN_OPTIONS=halt_on_error=1`。

只有 `tests/vendor/kcp/ikcp.c` 关闭 `null,object-size` 检查，因为原版使用
空指针成员偏移宏。arknet 自有实现保留这两项检查。Apple ASan 通过不等同于
Linux LeakSanitizer 检查通过。

## 范围

本地测试不能证明 WAN 容量、生产时长稳定性、身份认证或 FEC 行为。
[RUDP 性能报告](../../../../docs/performance/rudp_CN.md)单独保存测量结果、
机器信息和永久 C++ 基准程序。
