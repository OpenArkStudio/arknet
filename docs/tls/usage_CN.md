# TLS 用法

两种 Asio 后端都可启用 TLS，并通过 `arknet::arknet` 接口目标传递配置。CMake 查找并链接 OpenSSL 1.1.1 或更新版本。安装与后端配置见[构建指南](../guide_CN.md)；各编译单元必须使用一致的设置。

```sh
cmake -S . -B build-tls -DARKNET_ENABLE_SSL=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
```

## 安全 TCP 回显服务端

开启 TLS 后，通过 `arknet::arknet` 接口目标，将两段代码分别构建为 C++20 可执行程序。示例使用仓库中的本地测试 CA 和服务端证书。可在仓库根目录运行，或分别通过两个程序的第一个参数指定测试证书目录。公开测试私钥不能用于部署。服务端测试证书包含 `localhost` 和 `127.0.0.1` 身份。先启动服务端，再在另一个终端运行客户端。在服务端终端按 Enter 停止服务端。

```cpp
#include <arknet/tcp/tcps_server.hpp>
#include <iostream>
#include <string>
#include <string_view>

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

## 要求客户端证书

mTLS 需要在示例服务端启动之前加入：

```cpp
arknet::error_code server_error;
server.load_verify_file(certs + "/ca.pem", server_error);
if (server_error)
    return 1;
server.set_verify_mode(asio::ssl::verify_peer |
    asio::ssl::verify_fail_if_no_peer_cert, server_error);
if (server_error)
    return 1;
```

创建客户端后、启动客户端前，配置客户端证书：

```cpp
client.set_cert_file(certs + "/ca.pem", certs + "/client.pem",
    certs + "/client-key.pem", "");
if (arknet::get_last_error())
    return 1;
```

保留客户端对服务端身份的校验。没有可信客户端证书时，服务端拒绝连接。TLS 1.3 客户端可能短暂报告启动成功后才收到拒绝，因此 mTLS 准入需要处理断开事件和业务就绪状态，不能只依赖 `start` 返回值。应用权限应单独映射到经过校验的证书身份。

## 对 WSS 与 HTTPS 使用相同规则

WSS 使用 `<arknet/http/wss_client.hpp>` 和 `<arknet/http/wss_server.hpp>` 中的 `wss_client`、`wss_server`。按相同方式配置证书和 CA 信任。服务端启动不传 `use_dgram`，客户端使用 `(host, port, "/echo")`。WebSocket 自行提供消息边界，并在 TLS 之后执行 HTTP 升级。帧类型和升级头见 [WebSocket 用法](../websocket/usage_CN.md)。

HTTPS 使用 `https_client`、`https_server` 和对应头文件。证书与 mTLS 规则保持一致，收发数据则是 HTTP 请求和响应对象，不是字节 view。详见 [HTTP 用法](../http/usage_CN.md)。HTTPS 不会自动加入 WebSocket 升级路由，也不改变业务授权规则。

所有安全端点族都提供 `bind_handshake` 和 `ssl_stream()`。诊断握手错误时，在客户端启动前注册回调：

```cpp
client.bind_handshake([&]
{
    const auto error = arknet::get_last_error();
    if (error)
        std::cerr << "TLS handshake: " << error.message() << '\n';
});
```

CA 不可信与 DNS/IP 身份不匹配是不同错误。应检查传给 `start` 的 host、证书 subject alternative name、信任配置和有效期。使用数字 IP 连接时，证书需要包含对应 IP 身份；设置 DNS SNI 名称不能替代 IP 校验。

## 关闭与扩展

安全发送保留普通传输的负载所有权、完成回调签名和容量限制。TLS 写入完成不代表远端业务应答。启动前配置 `set_connect_timeout` 和 `set_disconnect_timeout`；关闭时在断开期限内尝试 TLS close-notify。WSS 先关闭 WebSocket 协议，再关闭 TLS 和 TCP。

通过 `tcps_client_t`、`tcps_session_t` 和 `tcps_server_t` 进行 CRTP 扩展；WSS 和 HTTPS 提供对应的 `_t` 类型。可通过继承使用原生 SSL context 选项。启动前配置这些选项，并在端点 IO 通道串行访问 stream。重连会创建新的 TLS stream 并重新校验，不能继续使用旧的原生 stream 引用。

IO 回调内请求停止，由所有者线程等待。外部 context 必须运行到停止完成；先销毁端点，再停止外部运行线程。另见 [IO 模型](../threading_CN.md)、[安全回归测试](https://github.com/OpenArkStudio/arknet/blob/main/tests/tls.cpp)及[性能](performance_CN.md)。
