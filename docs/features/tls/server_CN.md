# 创建 TLS TCP 服务端

按[安装与配置](../../get-started/installation_CN.md)接入 C++20 应用。先启动服务端，再运行[对应客户端](client_CN.md)。

## 安全 TCP 回显服务端

开启 TLS 后，通过 `arknet::arknet` 接口目标，将本页服务端与[客户端](client_CN.md)分别构建为 C++20 可执行程序。示例使用仓库中的本地测试 CA 和服务端证书。可在仓库根目录运行，或分别通过两个程序的第一个参数指定测试证书目录。服务端测试证书包含 `localhost` 和 `127.0.0.1` 身份。先启动服务端，再在另一个终端运行客户端。在服务端终端按 Enter 停止服务端。

<Aside type="caution" title="注意：公开测试私钥">

`tests/certs/` 中的证书和私钥是公开测试材料。生产环境使用实际部署的证书链和私钥。

</Aside>

```cpp
#include <iostream>
#include <string>
#include <string_view>

#include <arknet/tcp/tcps_server.hpp>

int main(int argc, char** argv)
{
    const std::string certs = argc > 1 ? argv[1] : "tests/certs";
    arknet::tcps_server server;
    server.set_cert_file("", certs + "/server.pem", certs + "/server-key.pem", "");
    if (arknet::get_last_error())
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    server.bind_recv([](auto& session, std::string_view bytes)
    {
        session->async_send(bytes);
    });
    if (!server.start("127.0.0.1", 7443, arknet::use_dgram))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "TLS TCP listening on 127.0.0.1:7443\n";
    std::cin.get();
    server.stop();
}
```


[协议配置与进阶使用](configuration_CN.md) · [性能](performance_CN.md)
