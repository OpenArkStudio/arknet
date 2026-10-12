# 示例

仓库包含小型 C++ 应用，各协议指南另提供完整的客户端与服务端程序。
两端使用一致的后端与 TLS 设置。

## 仓库程序

| 程序 | 源码 | 运行 |
| --- | --- | --- |
| TCP 回显服务端 | [echo_server.cpp](../../examples/echo_server.cpp) | `build/examples/arknet_echo_server` |
| HTTP 回显服务端 | [http_echo_server.cpp](../../examples/http_echo_server.cpp) | `build/examples/arknet_http_echo_server` |
| IPv6 客户端与服务端 | [ipv6_echo.cpp](../../examples/ipv6_echo.cpp) | `build/examples/arknet_ipv6_echo` |

在仓库根目录构建：

```sh
cmake -S . -B build -DARKNET_BUILD_EXAMPLES=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
```

Boost 后端将 Asio include 参数替换为 `-DARKNET_USE_BOOST_ASIO=ON`。
Visual Studio 添加 `--config Release`，运行 `examples/Release/` 下的 `.exe`。
程序源码说明实际端口与停止方式。

## 协议程序

| 协议 | 服务端 | 客户端 |
| --- | --- | --- |
| TCP | [完整程序](../features/tcp/server_CN.md) | [完整程序](../features/tcp/client_CN.md) |
| UDP | [完整程序](../features/udp/server_CN.md) | [完整程序](../features/udp/client_CN.md) |
| WebSocket | [完整程序](../features/websocket/server_CN.md) | [完整程序](../features/websocket/client_CN.md) |
| HTTP | [完整程序](../features/http/server_CN.md) | [完整程序](../features/http/client_CN.md) |
| TLS TCP | [完整程序](../features/tls/server_CN.md) | [完整程序](../features/tls/client_CN.md) |

[HTTPS](../features/http/configuration_CN.md)与[WSS](../features/websocket/configuration_CN.md)
说明如何调整这些完整程序。安全传输需要 OpenSSL 与 `ARKNET_ENABLE_SSL=ON`；
仓库私钥仅用于本地测试。

性能程序位于 `benchmarks/`，见[测量方法](../performance/methodology_CN.md)。
