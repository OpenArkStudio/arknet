# 第一个客户端与服务端

下面的完整程序启动本地 TCP 回显服务端与客户端，验证回复后停止两者。

<Steps>

<Step title="创建应用项目">

按[安装与配置](installation_CN.md)安装依赖。在 `main.cpp` 所在目录创建
`CMakeLists.txt`，并将 `/path/to/arknet` 改为实际源码路径：

```cmake
cmake_minimum_required(VERSION 3.21)
project(arknet_echo LANGUAGES CXX)
add_subdirectory(/path/to/arknet arknet-build)
add_executable(arknet_echo main.cpp)
target_link_libraries(arknet_echo PRIVATE arknet::arknet)
```

arknet 是 header-only 库；接口目标只传递构建配置和依赖。

</Step>

<Step title="添加客户端与服务端">

将下面的完整程序放入 `main.cpp`：

```cpp
#include <chrono>
#include <future>
#include <string>

#include <arknet/arknet.hpp>
using namespace std::chrono_literals;

int main()
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data)
    {
        session->async_send(data);
    });
    if (!server.start("127.0.0.1", 0, arknet::use_dgram))
        return 1;

    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    std::promise<std::string> reply;
    client.bind_recv([&](std::string_view data)
    {
        reply.set_value(std::string(data));
    });
    if (!client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram))
        return 2;
    auto result = reply.get_future();
    if (!client.async_send("hello"))
        return 3;
    const bool ready = result.wait_for(5s) == std::future_status::ready;
    const bool correct = ready && result.get() == "hello";
    client.stop();
    server.stop();
    return correct ? 0 : 4;
}
```

</Step>

<Step title="构建并检查回显">

```sh
cmake -S . -B build -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
./build/arknet_echo
```

退出码为 `0` 表示客户端收到 `hello`，且两个端点已停止。
使用 Visual Studio 时，构建命令添加 `--config Release`，运行 `build/Release/arknet_echo.exe`。

</Step>

</Steps>

<Aside type="note" title="说明：TCP 消息边界">

两端都使用 arknet 的 `use_dgram` 消息拆包。TCP 本身是字节流，应按对端
线路协议选择[拆包方式](../features/tcp/index_CN.md)。等待发生在所有者线程，不在 IO 回调中。

</Aside>

## 下一步

按业务选择 [TCP](../features/tcp/index_CN.md)、[UDP](../features/udp/index_CN.md)、
[WebSocket](../features/websocket/index_CN.md)、[HTTP 与路由](../features/http/index_CN.md)或
[TLS](../features/tls/index_CN.md)。将对象用于服务前，阅读
[生命周期与发送](../concepts/sending_CN.md)；共享 context 前，阅读
[IO 模型](../concepts/io-models_CN.md)。
