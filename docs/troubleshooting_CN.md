# 故障排查

在发生错误的回调中记录错误。`get_last_error()` 使用线程局部存储，其他线程的
错误值不能反映失败的操作。发送操作记录完成回调的显式 `error_code` 和字节数。
执行和停止约定见[运行时](runtime_CN.md)。

## 按现象检查

| 现象 | 先检查 | 处理方式 |
| --- | --- | --- |
| 找不到 `asio.hpp` | 确认选择了 standalone 模式，并查找已安装的头文件 | 将 `ARKNET_ASIO_INCLUDE_DIR` 设置为包含 `asio.hpp` 的目录，再重新配置；见[安装](guide_CN.md) |
| CMake 找不到 Boost 或 OpenSSL | 检查要求的版本、已安装软件包和 CMake 前缀 | 设置 `CMAKE_PREFIX_PATH` 或 `OPENSSL_ROOT_DIR`；切换后端时使用独立构建目录 |
| Asio 和 Beast 的 socket 类型不匹配 | 查看详细构建输出中的编译宏，以及更早包含的后端头文件 | 全部翻译单元使用同一后端，通过 `arknet::arknet` 接口目标传递配置宏 |
| 安全传输头文件或 OpenSSL 符号不可用 | 检查 `ARKNET_ENABLE_SSL`、OpenSSL 头文件和链接库 | 在 CMake 中启用 TLS 并使用导出的接口目标；直接使用头文件时，定义宏并提供 OpenSSL 依赖 |
| TLS 握手拒绝证书 | 记录回调错误，检查信任链、证书有效期和请求的 DNS/IP 身份 | 加载所需 CA，使用证书 subject alternative names 中的名称或地址连接，并检查系统时钟 |
| TLS 服务端启动失败，或报告 `invalid_argument` | 加载证书和私钥后，立即检查错误 | 启动前加载可用证书及匹配的私钥；mTLS 还需配置客户端 CA 和服务端验证模式 |
| `async_send()` 返回 `false`，错误为 `no_buffer_space` | 检查字节上限、排队字节数和应用未完成的请求数 | 控制生产速率或降低并发；完成回调释放容量后，按应用策略重试 |
| `send()` 返回零，错误为 `in_progress` | 检查调用是否位于对象 context 的工作线程 | 使用 `async_send()` 观察异步完成结果；不要在同一 context 的工作线程等待 future |
| 定时器查询返回默认值，错误为 `operation_not_supported` 或 `operation_aborted` | 检查是否从同一 context 的另一个 strand 查询，或 context 已停止 | 在对象的 executor 查询，或在运行线程持续运行时由所有者线程查询 |
| 外部 IO 模式下，启动、future 或停止一直等待 | 检查 `run()` 线程和 work guard 是否仍有效 | 保持宿主 context 运行，直到对象停止完成；按[停止顺序](runtime_CN.md#生命周期和执行上下文)处理 |
| TCP 接收回调拆分或合并了应用消息 | 检查两端的拆包配置 | 将原始 TCP 视为字节流，或在两端使用一致的分隔符、自定义拆包或 `use_dgram`；见 [TCP 用法](tcp/usage_CN.md) |
| 从磁盘打开 Docsify 后页面空白 | 检查浏览器控制台中被阻止的 Markdown 或 CDN 请求 | 通过 HTTP 提供 `docs/`，并确认固定版本的 CDN 资源可以访问 |

发送完成且没有错误，只表示传输完成，不表示对端应用已经处理消息。需要确认处理
结果时，由应用协议提供响应，并记录和验证响应。

## 本地运行文档

在仓库根目录执行：

```sh
python3 -m http.server 8000 --directory docs
```

打开 [http://127.0.0.1:8000/](http://127.0.0.1:8000/)。Docsify 在运行时请求
Markdown，因此文档必须通过 HTTP 提供。端口已占用时，选择其他端口。
顶部语言切换会保留当前页面。

## 提供复现证据

报告问题时，提供以下信息：

- 源码版本，以及最小客户端、服务端或测试用例。
- 操作系统、架构、编译器和依赖版本。
- Asio 后端、TLS 配置和 CMake 配置命令。
- 内置或外部 IO、context 和通道数量，以及调用 `run()` 的线程数量。
- 在错误发生处记录的错误 category、数值和消息。
- 发送问题所涉及的有效载荷大小、拆包方式、队列上限和完成字节数。
- 测试日志或 benchmark JSON，以及实际执行的命令。

当前验证证据和复现命令见[测试](testing_CN.md)。仓库中的证书和私钥仅用于测试；
排查 TLS 时，使用部署环境所需的信任和身份配置。
