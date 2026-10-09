# HTTP 路由

[HTTP 与 HTTPS](../http_CN.md)

`arknet::http_router` 为现有 HTTP/1 和 HTTPS 端点提供 header-only 路由。
它同步分发完整请求，返回独立拥有数据的 `http::response<http::string_body>`，
无需增加依赖。

## 注册与绑定

```cpp
#include <arknet/arknet.hpp>
#include <arknet/http/router.hpp>

auto router = std::make_shared<arknet::http_router>();
router->add(arknet::http::verb::get, "/users/:id",
    [](const arknet::http_route_context& context,
       arknet::http_route_response& response)
    {
        response.set(arknet::http::field::content_type, "text/plain");
        response.body() = std::string(context.param("id"));
    });

arknet::http_server server;
arknet::bind_router(server, router);
server.start("127.0.0.1", 8080);
```

`bind_router` 捕获 `shared_ptr<const http_router>`，在服务器使用接收回调期间
保留路由器。它也适用于 `https_server` 以及自定义 HTTP/HTTPS session。
证书和对端验证使用现有 TLS API 配置。绑定会替换服务器的接收回调；也可以
在应用的 `bind_recv` 回调中调用 `router(request)`，将返回值交给
`session->async_send`。

开始处理请求前，完成路由、中间件和错误回调的配置。配置修改及路由器移动
不得与请求分发同时进行。完成配置后支持并发分发；处理函数和中间件共享的
可变业务状态需要自行同步。回调支持 move-only 对象。路由器可以移动，不可复制。

## 路径与上下文

| 模式 | 匹配规则 | 示例 |
| --- | --- | --- |
| `/users/new` | 静态路径 | `/users/new` |
| `/users/:id` | 一个非空路径段 | `/users/42` 得到 `id = "42"` |
| `/files/*tail` | 剩余路径段，可为空 | `/files/a/b` 得到 `tail = "a/b"` |

参数名以字母或下划线开头，只能包含 ASCII 字母、数字和下划线。同一模式内
参数名必须唯一。通配符只能放在末尾。无效模式以及重复的「方法 + 模式」注册
抛出 `std::invalid_argument`；仅修改参数名不构成不同模式。

匹配从左到右逐段比较，静态段优先于参数段，参数段优先于通配符。完整路径
优先于捕获空值的通配符。先选择最具体的资源，再选择该资源的方法。例如，
同时注册 `GET /users/new` 和 `POST /users/:id` 后，`POST /users/new` 返回
405，不会继续匹配参数路由。注册顺序不影响结果。路径区分大小写，保留连续
斜杠和尾斜杠。

`http_route_context` 提供以下成员：

| 成员 | 含义 |
| --- | --- |
| `request` | 完整 Beast 请求的 const 引用 |
| `path` | 用于路由匹配的已解码路径 |
| `query` | 原始查询文本，不含 `?`，不进行表单解码 |
| `parameters` | 所选路由的参数名和值 |
| `param(name)` | 参数值；参数不存在时返回空视图 |

上下文及其中的视图仅在同步回调期间有效。需要长期保留的值必须复制。
响应独立拥有头字段和正文，可以移动给 `async_send`。路由器设置 HTTP 版本，
初始连接策略沿用请求，并生成响应消息边界。处理函数可以设置
`keep_alive(false)`，使连接在响应发送后关闭。

请求目标必须是以 `/` 开头的 origin-form 路径，或 OPTIONS 使用的 `*`。
代理的 absolute-form 目标和 CONNECT 的 authority 目标返回 400。
路径仅进行一次百分号解码；查询文本保留原始字节和 `+`。无效转义、原始
非 URI 字符、控制字符、fragment、反斜杠、路径中编码的斜杠或反斜杠，
以及解码后的 `.`/`..` 路径段，均在执行中间件前返回 400。
百分号编码的非 ASCII 字节原样保留，不进行 Unicode 归一化或 UTF-8 校验。
这些规则用于路由校验，不提供文件系统沙箱或表单、查询参数解析。

## 方法与中间件

资源不存在时返回 404。所选资源未注册请求方法时，返回 405，并附带
`Allow` 头。HEAD 优先使用显式 HEAD 处理函数，否则使用 GET 处理函数。
响应不含正文；保留显式设置的 `Content-Length`，或根据 GET 表示计算长度。
HEAD 错误响应同样遵循此规则。

OPTIONS 优先使用显式处理函数。未注册时，已知资源返回 204 和 `Allow`；
不存在的资源返回 404。`OPTIONS *` 返回 204，并列出路由器已注册的方法。
自动生成的列表包含 OPTIONS；注册 GET 时也包含 HEAD。204、304 等
无正文状态不会发送响应正文。

```cpp
router->use([](const arknet::http_route_context& context,
               arknet::http_route_response& response)
{
    if (context.request[arknet::http::field::authorization].empty())
    {
        response.result(arknet::http::status::unauthorized);
        return false;
    }
    return true;
});
```

中间件按注册顺序执行，先于路由处理函数或自动生成的 404、405、OPTIONS
响应。返回 `true` 继续执行；返回 `false` 发送中间件当前的响应，并跳过
剩余步骤。存在所选处理函数时，可以访问该路由的参数，HEAD 回退也包含参数。
中间件和处理函数均在 session 的 IO 执行器上运行。长时间计算会占用该
执行器；此同步接口不提供异步续执行或流式传输。

处理函数和中间件抛出异常后，返回全新的 500 响应，正文为
`Internal Server Error`，不保留部分头字段，也不暴露异常详情。
可通过 `bind_error` 注册错误回调，在同一执行器上接收
`std::exception_ptr`；错误回调自身抛出的异常会被捕获。

```cpp
router->bind_error([](std::exception_ptr error)
{
    try { std::rethrow_exception(error); }
    catch (const std::exception& failure) { /* Record failure.what(). */ }
});
```

正式的[路由测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/router.cpp)
覆盖输入校验、路径匹配、中间件、异常、并发以及 HTTP/HTTPS 流水线。
构建并运行 `arknet_router_test`，或执行项目的 `arknet_check` 目标。
