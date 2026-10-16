# Redis 客户端与连接池

完整的[客户端](../../../examples/redis_client.cpp)与[连接池](../../../examples/redis_pool.cpp)示例只连接命令行指定的主机和端口。它们使用 `PING` 和二进制 `ECHO`，不修改键。Redis 是单独运行的服务，不是 arknet 提供的服务端端点。

## 编译与运行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_EXAMPLES=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build build --target arknet_redis_client arknet_redis_pool --config Release

build/examples/arknet_redis_client 127.0.0.1 6379
build/examples/arknet_redis_pool 127.0.0.1 6379
```

使用多配置生成器时，运行 `examples/Release/` 下的程序。使用 Standalone Asio 时配置 `ARKNET_USE_BOOST_ASIO=OFF`，必要时另设 `ARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`。示例不依赖脚本运行时。

启用认证时，通过进程环境变量 `ARKNET_REDIS_PASSWORD` 提供密码；ACL 用户还可设置 `ARKNET_REDIS_USERNAME`。程序不内嵌密码，也不接受密码命令行参数。若服务端 ACL 允许，可以指定用户名并使用空密码。

## 单条连接

包含 `<arknet/redis/client.hpp>`，配置 `connect_options`，通过 `async_connect` 连接成功后再发送命令。`async_execute` 提交一组命令参数；`async_pipeline` 提交多条命令，按顺序返回回复：

```cpp
const std::string payload("ark\0net", 7);
client.async_pipeline({{"PING"}, {"ECHO", payload}}, [](arknet::redis::result response) {
    if (!response)
    {
        // 操作失败时，已经发送的命令仍可能执行过。
        return;
    }
    // 先检查 response.replies[i].type，再读取对应的值。
});
```

参数是具有明确长度的 `std::string`，内含零字节或 CR/LF 不会改变参数边界。回复拥有自身数据。服务端的 `error` 或 `bulk_error` 保留在 `response.replies` 中；传输完成成功不代表每条 Redis 命令都执行成功。

完整示例通过 promise 在主线程等待。回调运行在连接的 IO 线程上，不要在其中等待 future。回调捕获的应用对象需要存活到回调完成。

独立客户端需要等待已受理 handler、其捕获及自有传输资源回收时，可在主线程调用 `close_and_wait()` 并检查返回值；自身 IO 线程和池内客户端不支持此等待。普通 `close()` 仍为异步关闭。应用退出前请先阅读[关闭约定](configuration_CN.md#执行与关闭)。

## 独占连接租约

包含 `<arknet/redis/pool.hpp>`，配置 `pool_options.connection` 后调用 `async_acquire`。成功的 `acquire_result` 包含只能移动的 `connection` 租约。通过租约提交命令，完成后调用 `release()` 或离开作用域；不应复用的连接调用 `discard()`。

```cpp
pool.async_acquire([](arknet::redis::pool::acquire_result response) {
    if (!response)
    {
        return;
    }
    auto connection = std::move(response.connection);
    connection.async_execute({"PING"}, [](arknet::redis::result reply) {
        // 处理回复，不在回调线程执行阻塞操作。
    });
    // 离开作用域后，等在途命令回调结束，再归还连接。
});
```

完整的 `WATCH`／`MULTI`／`EXEC` 序列需要持有同一个租约。归还时先等待在途回调及有序回复排空，再重置连接并恢复连接池配置的认证、协议和数据库，之后才能交给下一位使用者。重置失败或不被服务端支持时，丢弃并替换连接。

停机时先停止提交方，再在主线程调用 `pool.close_and_wait()`。成功返回前会等待此前受理的回调及其捕获释放、等待连接池共用工作线程退出，并回收所有仍存活子连接的传输资源，包括已丢弃但仍由旧租约持有的连接。保留的租约随后同步返回 `closed` 和请求 ID 0。在连接池自己的 IO 线程或重入调用中会拒绝等待；需要检查返回值，并遵守[关闭契约](configuration_CN.md#执行与关闭)。

## TLS 与 mTLS

两个示例均接受 `HOST PORT --tls [CA_FILE]`：

```sh
build/examples/arknet_redis_client redis.example.com 6380 --tls /path/to/ca.pem
build/examples/arknet_redis_pool redis.example.com 6380 --tls /path/to/ca.pem
```

这会选择 `redis::tls_client` 或 `redis::tls_pool`，证书链与 DNS/IP 身份验证保持启用。mTLS 在 `client_options` 中设置 `tls_certificate_file` 和 `tls_private_key_file`；连接池使用 `pool_options.client_configuration`。字段说明见[配置](configuration_CN.md#tls-选项)。
