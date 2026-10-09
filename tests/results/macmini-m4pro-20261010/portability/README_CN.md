[English](README.md) | [简体中文](README_CN.md)

# 可移植性回归检查

日期：2026-10-10。测试机器：Mac mini Mac16,11、Apple M4 Pro、14 核、
48 GiB 内存、macOS 27.0.1（26A434）。依赖：standalone Asio 1.38.2、
Boost 1.90.0、OpenSSL 3.6.4。Clang 构建使用 Apple Clang 21.0.0；
GCC 构建使用 Homebrew GCC 15.2.0 和 libstdc++。

| 配置 | 检查 / 用例 | 结果 | 日志 |
| --- | ---: | --- | --- |
| Clang、standalone、Release、TLS | 41 / 133 | 通过，退出码 0 | [完整检查](arknet-format-standalone.log) |
| Clang、Boost、Release、TLS | 41 / 133 | 通过，退出码 0 | [完整检查](arknet-format-boost.log) |
| Clang、standalone、Debug、ASan/UBSan、TLS | 16 / 133 | 通过，退出码 0 | [完整检查](arknet-format-asan.log) |
| Clang、standalone、Debug、TSan、TLS | 16 / 133 | 通过，退出码 0 | [完整检查](arknet-format-tsan.log) |
| GCC、standalone、NoTLS、生命周期 | 1 / 18 | 通过，退出码 0 | [生命周期](arknet-lifecycle-gcc-after.log) |
| GCC、standalone、NoTLS、TCP | 1 / 5 | 通过，退出码 0 | [TCP](arknet-ci-fix-gcc-tcp.log) |
| GCC、standalone、NoTLS、detail | 1 / 11 | 通过，退出码 0 | [detail](arknet-ci-fix-gcc-detail.log) |
| GCC、standalone、NoTLS、线程池 | 1 / 6 | 通过，退出码 0 | [线程池](arknet-ci-fix-gcc-io_pool.log) |

两种 Release 构建包含协议测试、压力测试、安装包使用验证和 25 个短基准检查。
Sanitizer 构建不包含基准程序，本次使用该 Mac 上的默认运行选项，未报告错误。
Apple ASan 通过不等同于 Linux LeakSanitizer 检查通过。

## 复现回归问题

libstdc++ 的 `std::packaged_task` 被取消并销毁后，尚未读取的 future
仍通过共享状态持有用户回调。[修复前日志](arknet-lifecycle-gcc-before.log)
记录了回调释放断言失败。修复将 promise 的结果状态与回调分开，覆盖 post、
线程池和排队任务。永久生命周期用例验证取消和完成后的捕获释放、
void／值／引用返回、仅可移动的捕获和结果，以及回调异常。

[GCC 命令记录](arknet-ci-fix-gcc-commands.txt) 包含实际编译与运行命令、
版本和退出状态。Clang 检查命令为：

```sh
cmake --build /private/tmp/arknet-macmini-standalone --parallel 2 --target arknet_check
cmake --build /private/tmp/arknet-macmini-boost --parallel 2 --target arknet_check
cmake --build /private/tmp/arknet-macmini-asan --parallel 2 --target arknet_check
cmake --build /private/tmp/arknet-macmini-tsan --parallel 2 --target arknet_check
```

MSVC 拆包条件修复显式返回函数指针，避免通过 `decltype(auto)` 推导函数引用。
已有 TCP 拆包用例覆盖该路径。这些本地结果不能替代 Windows、Linux runner 的结果。

UDP 发送失败时保留 socket 错误码，完成回调报告 0 字节。永久模拟测试覆盖
底层在 `message_size` 或取消错误时返回非零字节数的情况，以及成功发送、空报文、
连接式和指定端点两种入口、仅可移动的回调。真实 IPv4 测试仍验证 65,507 字节
发送成功、65,508 字节被拒绝。取消完成报告 0 字节，不能据此判断对端是否收到报文。

## 取消回调和 Windows 回归

[上一轮 CI](https://github.com/OpenArkStudio/arknet/actions/runs/38044247191)
发现排队的取消回调通过共享状态保留 context，形成循环引用。回调现使用弱引用。
永久回归用例覆盖 strand／直接执行器，以及尚未运行／已停止的 context：修复前
4 个 context 均未释放；修复后，两种后端的 4 个 context 均已释放。上述完整本地
检查包含该用例。两种后端的 Linux LeakSanitizer 也已通过下方的跨平台 CI 检查。

Windows job 还超过了 35 分钟限时。UDP 测试在
[standalone](windows-udp-before-standalone.log) 和 [Boost](windows-udp-before-boost.log)
下记录了 65,508 字节发送返回系统错误 1784。测试现检查发送失败、完成字节数为 0
及后续发送可恢复，并保留原生错误码。模拟完成测试也覆盖错误 1784。CI 限时为 60 分钟。

clang-format 18.1.8 检查和格式脚本自检均通过，覆盖全部 126 个项目 C++ 文件。
新安装目录中，第三方声明位于 `share/arknet/docs/`。

## 跨平台 CI

[CI 运行 38047445881](https://github.com/OpenArkStudio/arknet/actions/runs/38047445881)
对源码 `6d8b7224bd6dbbe34154d112caad0f49952e2897` 的 13 个 job 均通过：

- Linux：standalone 和 Boost，TLS 开启及关闭的 Release、启用泄漏检测的 ASan/UBSan，以及 TSan。
- macOS 和 Windows：standalone 和 Boost，开启 TLS 的 Release，包含安装包使用检查。
- 格式：clang-format 18 和格式脚本自检。

最后的文档和搜索索引更新，未改变下方源码校验值标识的实现、测试和构建配置。

[源码校验值](source.sha256) 标识当前实现、测试和格式配置；
[此前校验值](source-before-format.sha256) 对应保留的 132 用例日志。此前性能测量的原始输入
保留在 provenance 归档中，本次没有重跑性能矩阵。IPv6 仍仅验证本机回环，
范围见 [IPv6 测试记录](../ipv6/README_CN.md)。
