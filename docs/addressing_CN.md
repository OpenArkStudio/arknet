# IPv4 与 IPv6

TCP、UDP、HTTP/HTTPS、WebSocket/WSS 和 TLS 通过同一套 `start(host, port)`
接口接收 IPv4、IPv6 地址。UDP cast 也支持 IPv6 端点。

| 用途 | IPv4 | IPv6 |
| --- | --- | --- |
| 回环监听／连接 | `127.0.0.1` | `::1` |
| 监听所有接口 | `0.0.0.0` | `::` |
| HTTP Host／WebSocket authority | `127.0.0.1:8080` | `[::1]:8080` |
| URL | `http://127.0.0.1:8080/` | `http://[::1]:8080/` |

传给 `start` 的 IPv6 地址不加方括号，端口用单独参数传入。
HTTP 请求自行设置 `Host`；WebSocket 客户端构造握手地址。
域名可能解析到任一种地址族，`localhost` 不保证使用 IPv6。

## 客户端与服务端

完整的 [IPv6 回显示例](https://github.com/OpenArkStudio/arknet/blob/main/examples/ipv6_echo.cpp)
在 `::1` 上启动两端，检查回复后停止。构建并运行：

```sh
cmake --build build --target arknet_ipv6_echo
build/examples/arknet_ipv6_echo
```

[TCP](tcp/usage_CN.md)、[UDP](udp/usage_CN.md)、
[WebSocket](websocket/usage_CN.md) 和 [HTTP](http/usage_CN.md) 的客户端、服务端示例
可将两端回环地址改为 `::1`。HTTP 请求使用：

```cpp
request.set(arknet::http::field::host, "[::1]:8080");
```

TLS 客户端将 IP 地址与证书的 IP subject alternative name 校验，包括完整的 IPv6
地址。只有 `localhost` 或 `127.0.0.1` 身份的证书不能验证 `::1`。
身份配置见 [TLS 用法](tls/usage_CN.md)。独立的 `tests/certs/ipv6` 证书仅用于测试。

## 监听策略

绑定 `::` 会监听全部 IPv6 接口。是否接收 IPv4 映射连接取决于平台的
`IPV6_V6ONLY` 策略，单独绑定 `::` 不保证双栈服务。
需要明确支持两种地址族时，可分别监听 `0.0.0.0` 和 `::`，并将 IPv6 socket
设为只接收 IPv6。在 socket 打开后、绑定前，通过 `bind_init` 设置选项：

```cpp
server.bind_init([&]
{
    arknet::error_code error;
    server.acceptor().set_option(asio::ip::v6_only(true), error);
    if (error) throw arknet::system_error(error);
});
```

应用需要接受外部流量时才监听外部接口。UDP 多播配置区分地址族，IPv6 没有
IPv4 广播的等价功能。链路本地 IPv6 地址还需要指定接口作用域。

## 测试与基准

独立 doctest 程序为 `build/tests/arknet_ipv6_test`。
两个 C++ 基准和 shell 批量工具均支持 `--address-family ipv6`：

```sh
build/benchmarks/arknet_loopback_benchmark --protocol udp --address-family ipv6 \
  --payload 1024 --clients 16 --window 1 --io-model shared --io-threads 1 \
  --seconds 1 --warmup 0.25
build/benchmarks/arknet_loopback_benchmark --protocol https --address-family ipv6 \
  --certs tests/certs/ipv6 --payload 1024 --clients 16 --window 1 \
  --io-model shared --io-threads 1 --seconds 1 --warmup 0.25
build/benchmarks/arknet_coroutine_benchmark --protocol tcp --execution coroutine \
  --address-family ipv6 --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --seconds 1 --warmup 0.25
```

现有性能矩阵使用 IPv4 回环。`::1` 验证不代表跨主机吞吐量、IPv4/IPv6 性能一致、
多播可用性或所有平台的双栈行为。
