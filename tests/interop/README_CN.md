# RDP 端点互通测试

[English](README.md) | [中文](README_CN.md)

这些可选集成测试将真实 zephyr UDP/KCP 端点与 arknet RUDP 端点分进程配对。测试不将 zephyr 纳入仓库或安装包，zephyr 也不属于网络库依赖或默认测试依赖。

显式指定旧库的源码目录：

```sh
cmake -S . -B build/interop -DARKNET_BUILD_TESTS=ON \
  -DARKNET_ZEPHYR_ROOT=/path/to/zephyr
cmake --build build/interop --target arknet_zephyr_interop_check
```

设置 `ARKNET_USE_BOOST_ASIO=ON` 后，两个测试进程均使用 Boost.Asio；默认使用 Standalone Asio。CMake 在构建目录中修正旧 resolver 结果和继承构造声明，并在旧库测试目标中关闭过时的缓冲区重载。传入的 zephyr 源码目录保持原样。

| 配对 | 发起关闭的一方 |
| --- | --- |
| zephyr 客户端 / arknet 服务端 | 分别测试客户端和服务端 |
| arknet 客户端 / zephyr 服务端 | 分别测试客户端和服务端 |

每种配对均测试以下三个场景，并分别由客户端和服务端发起关闭，共执行 12 组进程配对：

| 场景 | 检查内容 |
| --- | --- |
| `baseline` | 一个会话，检查完整消息的内容和顺序、远端 FIN 及会话清理 |
| `reconnect` | 同一客户端连接同一服务端两次；每次会话重置消息索引，累计检查 64 条消息且无重复交付 |
| `impaired` | 双向每第 9 个 ARQ 数据报丢弃；每第 7 个符合条件的数据报延后，直至被下一包超越或等待达到 6 ms；检查实际丢包、乱序和完整有序交付 |

每个会话传输 32 条二进制消息，采用与 ARK 一致的 CS/SS 紧凑报头，覆盖 4096 字节消息体、嵌入 NUL 字节和 0–255 的字节值。消息长度超过 UDP 线上 MTU，验证 ARQ 分片。每个会话还检查两端均只有一次断连通知。

测试限制为 4 条在途应用消息，并设置 1 MiB socket 接收缓冲区，控制本机突发流量。`impaired` 场景在 arknet 进程内启动专用的测试转发器；其他场景不经过转发器。仅测试 ARQ 会话；FEC 是独立的可选协议，zephyr 不支持。旧 KCP 头文件使用的空指针成员偏移写法，与现有参考 KCP 测试一样，排除 null/object-size sanitizer 检查。

转发器保留 SYN/ACK/FIN 控制报文，仅对 ARQ 数据报施加丢包和乱序。FIN 是单向通知，没有确认或重传；FIN 丢失时，远端可能持续连接直到超时。这些测试验证数据恢复以及控制报文送达时的关闭流程，不能证明弱网下的可靠关闭。握手报文丢失后的重试由独立的 `rudp` 测试覆盖。

也可以直接运行两个可执行文件：

```sh
build/interop/tests/arknet_rudp_interop_peer server client reconnect | \
  build/interop/tests/arknet_zephyr_rdp_peer client client reconnect
build/interop/tests/arknet_zephyr_rdp_peer server server impaired | \
  build/interop/tests/arknet_rudp_interop_peer client server impaired
```

服务端通过标准输出发布本机临时端口。场景参数可以省略，默认为 `baseline`；两端必须指定相同场景和关闭发起方。CMake 分别检查两个进程的退出状态，每组最多等待 40 秒。ARK 业务集成仍需单独验证。
