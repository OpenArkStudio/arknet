# 创建 TLS TCP 客户端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动[对应服务端](server_CN.md)，再在另一个终端运行本页客户端。

启用 `ARKNET_ENABLE_SSL=ON` 并配置 OpenSSL。示例从 `tests/certs/ca.pem` 加载 CA；在仓库根目录运行，或通过第一个参数指定证书目录。服务端证书及 mTLS 配置见[协议配置](configuration_CN.md)与[服务端示例](server_CN.md)。公开测试证书和私钥仅用于本地测试。

## 安全 TCP 回显客户端

连接前加载 CA。客户端验证证书链，以及传给 `start` 的 `localhost` 身份；使用 DNS 名称连接时还会发送 SNI。证书不匹配时，应修正证书或连接名称，不应关闭对端校验。

```cpp
#include <arknet/tcp/tcps_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

int main(int argc, char** argv)
{
    const std::string certs = argc > 1 ? argv[1] : "tests/certs";
    std::promise<std::string> reply;
    auto received = reply.get_future();
    bool received_once = false;

    arknet::tcps_client client;
    client.set_auto_reconnect(false);
    arknet::error_code error;
    client.load_verify_file(certs + "/ca.pem", error);
    if (error)
    {
        std::cerr << error.message() << '\n';
        return 1;
    }
    client.bind_recv([&](std::string_view bytes)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(std::string(bytes));
        }
    });
    if (!client.start("localhost", 7443, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    if (!client.async_send(std::string("hello TLS")))
        return 1;

    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    const bool matched = ready && received.get() == "hello TLS";
    client.stop();
    std::cout << (matched ? "echo received\n" : "echo failed\n");
    return matched ? 0 : 1;
}
```

部署时，加载实际服务的证书链和匹配的 PEM 私钥。`set_cert_file` 最后一个参数为私钥密码，第一个参数可同时加载 CA 信任。`set_cert_buffer` 接受相同材料的内存 PEM 内容。调用便捷加载函数后，应立即检查 `get_last_error()`。继承的 Asio 方法同时提供抛异常和 `error_code` 重载；示例使用显式错误参数加载 CA。

客户端 `async_start` 发起连接和握手而不等待。返回值只表示发起结果；通过 `bind_handshake` 和 `bind_connect` 在 IO 通道观察 TLS 与连接最终结果。WSS 还会报告 HTTP 升级。


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
