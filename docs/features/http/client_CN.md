# 创建 HTTP 客户端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动[对应服务端](server_CN.md)，再在另一个终端运行本页客户端。

## 域名与默认端口

`http_client.start(host)` 和 `async_start(host)` 默认使用端口 **80**；
`https_client` 的对应接口默认使用 **443**。显式传入端口时使用指定值。
主机参数接受域名或不带方括号的 IPv4／IPv6 地址，不接受完整 URL。

```cpp
arknet::http_client client;
client.start("example.com");          // HTTP, port 80
// Or: client.start("example.com", 8080);
```

启用 TLS 后，HTTPS 使用独立的客户端类型。连接前配置可信 CA，证书链、域名校验和 SNI
按 [TLS 配置](../tls/configuration_CN.md)处理：

```cpp
arknet::https_client client;
client.load_verify_file("ca.pem");
client.start("example.com");          // HTTPS, port 443
```

长连接客户端发送的请求仍需设置 `Host` 字段。默认端口使用 `example.com`，
非默认端口使用 `example.com:8080`；IPv6 对应 `[::1]` 和 `[::1]:8080`。

## HTTP 回显客户端

客户端发送 `POST /echo`，并验证 HTTP 状态码和回显内容。主线程最多等待五秒；请求或验证失败时，返回非零状态。

```cpp
#include <chrono>
#include <future>
#include <iostream>
#include <utility>

#include <arknet/http/http_client.hpp>

int main()
{
    std::promise<bool> reply;
    auto received = reply.get_future();
    bool received_once = false;
    arknet::http_client client;
    client.set_auto_reconnect(false);
    client.bind_recv([&](arknet::http::response<arknet::http::string_body>& response)
    {
        if (!received_once)
        {
            received_once = true;
            reply.set_value(response.result() == arknet::http::status::ok &&
                response.body() == "hello HTTP");
        }
    });
    if (!client.start("127.0.0.1", 8080))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    arknet::http::request<arknet::http::string_body> request(
        arknet::http::verb::post, "/echo", 11);
    request.set(arknet::http::field::host, "127.0.0.1:8080");
    request.keep_alive(true);
    request.body() = "hello HTTP";
    request.prepare_payload();
    if (!client.async_send(std::move(request)))
        return 1;
    const bool ready = received.wait_for(std::chrono::seconds(5)) ==
        std::future_status::ready;
    const bool correct = ready && received.get();
    client.stop();
    std::cout << (correct ? "echo received\n" : "echo failed\n");
    return correct ? 0 : 1;
}
```


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
