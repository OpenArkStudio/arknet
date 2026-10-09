# 使用 HTTP 与 HTTPS

[介绍](../http_CN.md) | [路由](routing_CN.md) | [性能](performance_CN.md)

## HTTP 回显服务端

按[构建指南](../guide_CN.md)，通过 `arknet::arknet` 接口目标在 C++20 应用中接入网络库。将服务端和客户端代码块分别构建为可执行程序。先启动服务端，再在另一个终端运行客户端。在服务端终端按 Enter 停止服务端。普通 HTTP 不需要启用 TLS。

```cpp
#include <arknet/http/http_server.hpp>
#include <iostream>
#include <utility>

int main()
{
    arknet::http_server server;
    server.bind_recv([](auto& session, arknet::http::request<arknet::http::string_body>& request)
    {
        arknet::http::response<arknet::http::string_body> response(
            arknet::http::status::ok, request.version());
        response.keep_alive(request.keep_alive());
        response.set(arknet::http::field::content_type, "text/plain");
        response.body() = request.body();
        response.prepare_payload();
        session->async_send(std::move(response));
    });
    if (!server.start("127.0.0.1", 8080))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "HTTP listening on http://127.0.0.1:8080/echo\n";
    std::cin.get();
    server.stop();
}
```

## HTTP 回显客户端

客户端发送 `POST /echo`，并验证 HTTP 状态码和回显内容。主线程最多等待五秒；请求或验证失败时，返回非零状态。

```cpp
#include <arknet/http/http_client.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <utility>

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

## 消息、回调与所有权

客户端接收 `response<string_body>&`；服务端接收 Session 引用和
`request<string_body>&`。引用只在回调期间有效，保留前必须复制或移动。
接受入队的发送持有序列化消息至完成，调用方之后修改原对象不会改变排队消息。

修改 body 后调用 `prepare_payload()`，或明确配置 chunked 拆包。
写完成统计 HTTP 线上字节，包括头部；不代表对端已收到或处理请求。
立即拒绝和已入队完成遵循[公共发送约定](../runtime_CN.md)。

## Keep-Alive 与流水线

同一连接可连续排队多个请求。客户端按请求顺序匹配响应，跳过信息响应后报告
最终响应。服务端应用必须按请求顺序发送响应。路由框架的同步处理自然保持顺序；
将业务移到其他线程后，需要恢复响应顺序再发送。

HEAD 响应没有 body，可用 `empty_body` 响应声明表示资源的 Content-Length。
需要 EOF 的响应在写完后关闭连接，后续排队发送以取消完成。
服务端收到 `Expect: 100-continue` 时，先发送信息响应再等 body。

## 上限与错误

启动前配置 `set_http_header_limit(bytes)` 和 `set_http_body_limit(bytes)`。
默认分别为 8192 字节和 16 MiB。读取中修改上限从下一条消息的解析器生效。
解析／拆包错误和超限消息会关闭连接。404 等状态是正常 HTTP 响应，
业务应检查 `response.result()`。

连接错误通过 `bind_disconnect` 和回调线程中的 `get_last_error()` 检查。
一次性请求可以关闭自动重连。重连不会自动重放业务请求。

## HTTPS 服务端与客户端

开启 `ARKNET_ENABLE_SSL=ON` 后，对上面的两个完整 HTTP 程序作以下修改，即可用于 HTTPS。保留请求与响应回调、回显验证和关闭代码。以下公开测试证书与私钥只用于本地测试；部署时应使用实际服务的证书和 CA 信任。证书路径及 mTLS 配置见 [TLS 示例](../tls/usage_CN.md)。

在服务端程序中，将头文件替换为 `<arknet/http/https_server.hpp>`，并将服务端声明替换为：

```cpp
arknet::https_server server;
server.set_cert_file("", "tests/certs/server.pem", "tests/certs/server-key.pem", "");
if (arknet::get_last_error())
{
    std::cerr << arknet::get_last_error().message() << '\n';
    return 1;
}
```

改用 `server.start("127.0.0.1", 8443)` 监听，并将输出 URL 改为 `https://localhost:8443/echo`。

在客户端程序中，将头文件替换为 `<arknet/http/https_client.hpp>`，并将客户端声明替换为：

```cpp
arknet::https_client client;
arknet::error_code error;
client.load_verify_file("tests/certs/ca.pem", error);
if (error)
{
    std::cerr << error.message() << '\n';
    return 1;
}
```

改用 `client.start("localhost", 8443)` 连接，并将请求 `Host` 字段设为 `"localhost:8443"`。连接名称决定 DNS/IP 身份校验；HTTP Host 头不会改变 TLS 身份。客户端默认校验证书链和连接名称。在仓库根目录运行程序，以便解析测试证书路径。

## 自定义 Session

自定义 Session 继承 `http_session_t<MySession>`，
交给 `http_server_t<MySession>`；HTTPS 提供对应的 `https_*` 形式。
[HTTP 测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/http.cpp)
覆盖独立 raw 对端的 chunked、HEAD、上限、Expect 和流水线。

## 路由与业务处理

[`http_router`](routing_CN.md) 提供方法／路径分发与中间件。
路由回调在接收 lane 同步执行。较长 CPU 或阻塞任务需要应用工作池、
有界业务排队和响应排序，见 [IO 模型](../threading_CN.md)。
