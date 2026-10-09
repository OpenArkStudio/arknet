[English](README.md) | [简体中文](README_CN.md)

# IPv6 回环测试

验证日期：2026-10-10。环境：macOS 27.0.1 arm64、Apple Clang 21.0.0、
Release C++20、Standalone Asio 1.38.2、Boost 1.90.0、OpenSSL 3.6.4。

| 配置 | 用例 | 断言 | 结果 |
| --- | ---: | ---: | --- |
| Standalone，开启 TLS | 11 | 114 | 通过 |
| Boost，开启 TLS | 11 | 114 | 通过 |
| Standalone，关闭 TLS | 7 | 69 | 通过 |
| Boost，关闭 TLS | 7 | 69 | 通过 |
| 现有 WebSocket 测试，Standalone | 3 | 17 | 通过 |
| 现有 WebSocket 测试，Boost | 3 | 17 | 通过 |
| Standalone，ASan/UBSan，开启 TLS | 11 | 114 | 通过 |
| Standalone，TSan，开启 TLS | 11 | 114 | 通过 |

[永久 doctest 用例](../../../ipv6.cpp)覆盖 TCP、UDP session、UDP cast、
WebSocket、TLS、WSS、HTTP 和 HTTPS 的 `::1` 通信，并检查二进制内容、
空 UDP 数据报、地址解析和对端地址、HTTP 路由、带方括号和实际端口的
Host、TLS 的 IP 身份验证、IP 地址不发送 DNS SNI，以及拒绝仅含 IPv4
身份的证书。

修复前日志记录了三个 Host 断言失败：IPv6 WS、使用非默认端口的 IPv4
WS 和 IPv6 WSS。修复后，握手 authority 使用 `[::1]:port` 或
`127.0.0.1:port` 格式，IPv4 回归检查保留在永久测试中。UDP 地址访问器
也已兼容当前 Asio，并在地址转换成功后清除旧错误。

安装依赖后，在仓库根目录执行：

```sh
cmake -S . -B build-ipv6 -DARKNET_BUILD_TESTS=ON -DARKNET_ENABLE_SSL=ON -DARKNET_USE_BOOST_ASIO=OFF
cmake --build build-ipv6 --target arknet_ipv6_test
./build-ipv6/tests/arknet_ipv6_test
```

将 `ARKNET_USE_BOOST_ASIO` 设为 `ON` 可切换到 Boost；将
`ARKNET_ENABLE_SSL` 设为 `OFF` 可运行七个非 TLS 用例。TLS 用例使用独立的
[IPv6 测试证书](../../../certs/ipv6)，其中 `generate.sh` 可以重新生成
自签名测试身份和明确指定的信任锚。已有性能测量使用的证书未修改。

`asan.log` 保留一次不受 macOS 支持的 `detect_leaks=1` 启动失败；随后使用
`detect_leaks=0:halt_on_error=1` 和 `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`
通过。`tsan.log` 使用 `TSAN_OPTIONS=halt_on_error=1`，无并发错误报告。
Apple ASan 不提供本次泄漏检测。

## 基准入口验证

每种后端的 `*-protocol-probes.json` 包含 TCP、UDP、WebSocket、TLS、WSS、
HTTP 和 HTTPS 七次探测；`*-native-probes.json` 包含回调／协程、单 context
四线程／四个 context 各一线程、无业务计算／10,000 次运算共八次探测。
两后端共 30 次均通过。每次测量 0.2 秒、预热 0.1 秒，仅用于检查 IPv6
参数和 CPU 占用率公式，不用于性能结论。JSON 保留完整命令、机器配置和源码哈希。

CPU 百分比检查为 `CPU 时间 / cpu_elapsed_seconds × 100`，允许输出舍入误差
小于 0.001 个百分点。`address_family` 均为 `ipv6`；TLS 使用独立 IPv6 身份。
两后端的 `arknet_ipv6_echo` 示例也已编译并运行成功。

这些检查仅覆盖本机 IPv6 回环，不涵盖外部 IPv6 路由、带作用域的链路本地
地址、DNS 地址族回退或双栈通配监听。`v6_only` 沿用操作系统默认值。
