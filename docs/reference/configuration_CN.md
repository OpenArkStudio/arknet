# 构建选项与默认值

本页用于查询构建开关与公开默认值。应用接入步骤见
[安装与配置](../get-started/installation_CN.md)。

## CMake 选项

| 选项 | 默认值 | 作用 |
| --- | --- | --- |
| `ARKNET_USE_BOOST_ASIO` | `ON` | 使用 Boost.Asio 和 Boost.Beast |
| `ARKNET_ENABLE_SSL` | `OFF` | 启用 TLS、HTTPS 和 WSS，并链接 OpenSSL |
| `ARKNET_BUILD_TESTS` | 独立构建为 `ON`，嵌入构建为 `OFF` | 构建 doctest 和集成检查 |
| `ARKNET_BUILD_EXAMPLES` | 独立构建为 `ON`，嵌入构建为 `OFF` | 构建示例程序 |
| `ARKNET_BUILD_BENCHMARKS` | `OFF` | 构建正式基准测试程序 |

## 直接使用头文件时的宏

| 宏 | 含义 |
| --- | --- |
| `ARKNET_USE_BOOST_ASIO=1` | 默认值 `1` 使用 Boost；设为 `0` 使用 Standalone Asio（TCP、UDP、RUDP、TLS） |
| `ARKNET_ENABLE_SSL` | 定义即启用安全传输；关闭时不要定义此宏 |
| `ARKNET_HEADER_ONLY` | 内部依赖实现宏，不用于切换编译库 |
| `ARKNET_ENABLE_TIMER_CALLBACK_WHEN_ERROR` | 错误时调用定时器回调；回调中检查 `get_last_error()` |

全部翻译单元必须使用一致的依赖方案、TLS 和行为宏。
CMake 的 `ON`／`OFF` 值与 C++ 宏是否定义是不同的配置约定。

## 运行时默认值

| 设置 | 默认值 | 说明 |
| --- | --- | --- |
| 应用发送队列 | 每对象 16 MiB 包体、1024 个操作 | [所有权与背压](../concepts/sending_CN.md) |
| 自动重连 | 开启；重试间隔一秒 | [重连](../guides/reconnect_CN.md) |
| HTTP 头部／body 上限 | 8192 字节／16 MiB | [HTTP 配置](../features/http/configuration_CN.md) |
| UDP 客户端 `SO_REUSEADDR` | 关闭 | [UDP 配置](../features/udp/configuration_CN.md) |
| TLS 协议／客户端校验 | 最低 TLS 1.2；校验证书链与 DNS/IP 身份 | [TLS 配置](../features/tls/configuration_CN.md) |
| 内核 socket 缓冲 | 未显式设置时使用平台默认值 | [Socket 缓冲](../guides/socket-buffers_CN.md) |

基准程序请求的 socket 容量是测量配置，不是库的默认值。
[公共 API 索引](api_CN.md) · [错误](errors_CN.md)
