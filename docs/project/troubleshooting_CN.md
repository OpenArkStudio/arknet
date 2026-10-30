# 故障排查

在错误回调中记录线程局部的 `get_last_error()`；其他线程的值不能反映该错误。
发送时记录完成回调的显式 `error_code` 与字节数。执行与停止约定见[运行时](../concepts/lifecycle_CN.md)。

## 按现象检查

| 现象 | 先检查 | 处理方式 |
| --- | --- | --- |
| 找不到 `asio.hpp` | 是否选用 Standalone Asio，以及已安装头文件的位置 | 将 `ARKNET_ASIO_INCLUDE_DIR` 设置为包含 `asio.hpp` 的目录，再重新配置；见[安装](../get-started/installation_CN.md) |
| CMake 找不到 Boost 或 OpenSSL | 要求的版本、已安装软件包与 CMake 前缀 | 设置 `CMAKE_PREFIX_PATH` 或 `OPENSSL_ROOT_DIR`；切换依赖方案时使用独立构建目录 |
| Asio 和 Beast 的 socket 类型不匹配 | 详细构建输出的编译宏，以及先前包含的依赖头文件 | 全部翻译单元使用同一依赖方案，通过 `arknet::arknet` 接口目标传递配置宏 |
| 安全传输头文件或 OpenSSL 符号不可用 | `ARKNET_ENABLE_SSL`、OpenSSL 头文件与链接库 | 在 CMake 中启用 TLS 并使用导出的接口目标；直接使用头文件时，定义宏并提供 OpenSSL 依赖 |
| TLS 握手拒绝证书 | 回调错误、信任链、证书有效期与请求的 DNS/IP 身份 | 加载所需 CA，使用证书 subject alternative names 中的名称或地址连接，并检查系统时钟 |
| TLS 服务端启动失败，或报告 `invalid_argument` | 加载证书与私钥后立即读取的错误 | 启动前加载可用证书及匹配的私钥；mTLS 还需配置客户端 CA 和服务端验证模式 |
| `async_send()` 返回 `false`，错误为 `no_buffer_space` | 字节上限、排队字节数与未完成应用请求数 | 控制生产速率或降低并发；完成回调释放容量后，按应用策略重试 |
| `send()` 返回零，错误为 `in_progress` | 是否在对象 context 的工作线程调用 | 使用 `async_send()` 观察异步完成结果；不要在同一 context 的工作线程等待 future |
| 定时器查询返回默认值，错误为 `operation_not_supported` 或 `operation_aborted` | 是否跨同一 context 的 strand 查询，或 context 已停止 | 在对象的 executor 查询，或在运行线程持续运行时由所有者线程查询 |
| 外部 IO 模式下，启动、future 或停止一直等待 | `run()` 线程与 work guard 是否有效 | 保持宿主 context 运行，直到对象停止完成；按[停止顺序](../concepts/lifecycle_CN.md#%E7%94%9F%E5%91%BD%E5%91%A8%E6%9C%9F%E5%92%8C%E6%89%A7%E8%A1%8C%E4%B8%8A%E4%B8%8B%E6%96%87)处理 |
| TCP 接收回调拆分或合并了应用消息 | 两端的拆包配置 | 将原始 TCP 视为字节流，或在两端使用一致的分隔符、自定义拆包或 `use_dgram`；见 [TCP 用法](../features/tcp/configuration_CN.md) |
| 本地文档返回 404 | 预览 URL 与服务端输出 | 使用下方 Nimbus 开发服务，访问 `/arknet/` |

发送无错误仅表示传输完成。确认对端应用处理结果时，须记录并验证应用协议响应。

<Aside type="danger" title="错误：TLS 证书校验失败">

记录 `bind_handshake` 中的错误，检查 CA 信任链、有效期和请求的 DNS/IP 身份。
修正证书或连接名称，并保持对端校验开启。

</Aside>

## 本地运行文档

<Steps>

<Step title="安装文档依赖">

使用 Node.js 22.12 或更新版本，在仓库根目录执行：

```sh
cd docs/site
npm ci
```

</Step>

<Step title="启动预览">

```sh
npm run dev -- --host 127.0.0.1 --port 8767
```

打开 [http://127.0.0.1:8767/arknet/](http://127.0.0.1:8767/arknet/)。
端口已占用时选择其他端口。顶部语言下拉框保留当前页面。

</Step>

<Step title="验证文档">

```sh
npm run check
```

该命令检查内容转换、类型、静态构建、链接和 AI Markdown 导出。

</Step>

</Steps>

## 提供复现证据

报告问题时提供：

- 源码版本，以及最小客户端、服务端或测试用例。
- 操作系统、架构、编译器和依赖版本。
- Asio 依赖方案、TLS 配置和 CMake 配置命令。
- 内置或外部 IO、context 和通道数量，以及调用 `run()` 的线程数量。
- 在错误发生处记录的错误 category、数值和消息。
- 发送问题所涉及的有效载荷大小、拆包方式、队列上限和完成字节数。
- 测试日志或 benchmark JSON，以及实际执行的命令。

验证证据与复现命令见[测试](testing_CN.md)。仓库证书与私钥仅用于测试；排查 TLS 时须使用部署环境的信任与身份配置。
