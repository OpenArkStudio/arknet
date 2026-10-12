# 发送单次 HTTP 请求

`arknet::http_request` 在外部 `io_context` 上执行一次普通 HTTP/1.0 或 HTTP/1.1 请求。需要限定请求生命周期时使用此接口；长连接和流水线请求使用 `http_client`。

## 请求与响应

```cpp
#include <iostream>
#include <utility>

#include <arknet/http/request.hpp>

int main()
{
    asio::io_context context;
    arknet::http_request operation(context);
    arknet::http_request::request_type request(
        arknet::http::verb::post, "/echo", 11);
    request.set(arknet::http::field::content_type, "text/plain");
    request.body() = "hello HTTP";
    auto reply = operation.async_request(
        "127.0.0.1", "8080", std::move(request), asio::use_future);
    context.run();
    auto [error, response] = reply.get();
    if (error)
    {
        std::cerr << error.message() << '\n';
        return 1;
    }
    std::cout << response.result_int() << ' ' << response.body() << '\n';
    return response.result() == arknet::http::status::ok ? 0 : 1;
}
```

操作对象持有请求数据。未提供 `Host` 字段时，接口根据主机名和数字端口补全；请求体长度等报文格式自动准备。请求使用 `Connection: close`。请求目标需要提前完成百分号编码。Cookie 等字段使用 Beast 的请求接口：

```cpp
arknet::http_request::request_type request(
    arknet::http::verb::get, "/items?name=a%20b", 11);
request.set(arknet::http::field::cookie, "session=abc; theme=dark");
```

GET、POST、PUT、DELETE 等方法使用同一接口。非零错误码表示传输、超时、取消、校验或解析失败。完整的 `404`、`500` 响应不会产生传输错误，业务成功条件由调用方根据状态码判断。

## 超时与限制

将 `http_request_options` 放在回调或 `asio::use_future` 参数之前。

| 配置项 | 默认值 | 作用范围 |
| --- | --- | --- |
| `connect_timeout` | 5 秒 | DNS 解析和 TCP 连接的总时限 |
| `io_timeout` | 20 秒 | 写入请求和接收完整响应的总时限 |
| `header_limit` | 8192 字节 | 响应头 |
| `body_limit` | 8 MiB | 响应体 |

超时值必须为正数，`header_limit` 不能为零。超时返回 `asio::error::timed_out`。接收限制与操作系统的套接字缓冲区独立，不限制发送的请求体。

## 回调与取消

```cpp
operation.async_request("127.0.0.1", "8080", std::move(request),
    [](arknet::error_code error, arknet::http_request::response_type response)
    {
        // Inspect error and response.result() here.
    });
// operation.cancel();
```

完成回调在操作对象的 strand 上执行一次；多个线程运行同一个 context 时仍保持串行。回调可以持有只能移动的对象。future 重载返回 `std::pair<error_code, response_type>`，通过其中的错误码报告失败。

`cancel()` 和 `stop()` 非阻塞且可重复调用。销毁对象会取消尚未完成的请求。取消返回 `asio::error::operation_aborted`，后续到达的套接字或定时器回调不会再次触发完成通知。在同一对象上再次发起请求会返回 `asio::error::already_started`；下一次请求应创建新对象。

保持 context 存活并持续运行，直到完成通知。直接停止 context 会阻止队列中的回调执行。此单次请求接口用于普通 HTTP，HTTPS 使用 `https_client`。`101 Switching Protocols` 等协议升级会被拒绝，WebSocket 使用 `ws_client`。

## 测试

`tests/httprequest.cpp` 覆盖请求数据所有权、不同方法与状态码、二进制响应体、临时响应、分块和关闭连接定界的响应体、HEAD、报文格式错误、大小限制、独立超时、取消和多 I/O 线程。
