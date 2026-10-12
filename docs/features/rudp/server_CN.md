# 创建 RUDP 服务端

按[安装与配置](../../get-started/installation_CN.md)接入库，将本页服务端和
[客户端](client_CN.md)分别编译为可执行程序，先启动服务端。

```cpp
#include <iostream>
#include <string_view>

#include <arknet/rudp/rudp_server.hpp>

int main()
{
    arknet::rudp_server server;
    server.bind_recv([](auto& session, std::string_view message)
    {
        session->async_send(message);
    });
    if (!server.start("127.0.0.1", 7000))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "RUDP listening on 127.0.0.1:7000\n";
    std::cin.get();
    server.stop();
}
```

`bind_recv` 每次收到一条完整应用消息。view 只在回调期间有效，`async_send`
会复制消息并持有发送数据。

通过 `bind_handshake([](auto& session, arknet::error_code error) { ... })`
观察会话建立。`bind_connect` 与 `bind_disconnect` 接收 Session 引用。
`set_rudp_options` 必须在启动前配置，新 Session 继承该配置。
使用 IPv6 时，两端地址改为 `::1`。

Session 共用服务端 UDP socket。队列限额、关闭通知和外部 context 生命周期
见[配置](configuration_CN.md)。
