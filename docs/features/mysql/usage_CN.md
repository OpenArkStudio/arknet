# MySQL 查询、预处理与事务

完整的 [C++ 示例](../../../examples/mysql_client.cpp)只连接调用者指定的主机和端口，依次验证文本查询，在显式事务中执行二进制安全的预处理参数，提交后关闭语句。示例使用字面量 `SELECT` 表达式，不创建表，也不修改持久化数据。

## 编译与运行

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_EXAMPLES=ON \
  -DARKNET_USE_BOOST_ASIO=ON
cmake --build build --target arknet_mysql_client arknet_mysql_pool --config Release

export ARKNET_MYSQL_USERNAME=application_user
export ARKNET_MYSQL_CA_FILE=/path/to/ca.pem
# 通过进程环境提供 ARKNET_MYSQL_PASSWORD。
build/examples/arknet_mysql_client mysql.example.com 3306
build/examples/arknet_mysql_pool mysql.example.com 3306
```

使用多配置生成器时，程序位于 `examples/Release/`。使用 Standalone Asio 时配置 `ARKNET_USE_BOOST_ASIO=OFF`，必要时指定 `ARKNET_ASIO_INCLUDE_DIR`。运行程序不需要脚本运行时。

示例接受 `ARKNET_MYSQL_PASSWORD`，以及可选的 `ARKNET_MYSQL_DATABASE`、`ARKNET_MYSQL_CA_FILE` 和 `ARKNET_MYSQL_SERVER_NAME`。程序不内嵌密码，也不接受密码命令行参数。连接要求 TLS；不指定 CA 文件时使用默认信任配置，私有 CA 签发的服务端证书需要对应 CA 文件。示例不会为了连接而关闭证书检查。

## 查询与结果

包含 `<arknet/mysql/connection.hpp>`，在 `async_connect` 成功后调用 `async_query(sql, handler, options)` 执行单条查询。`query_result` 包含 `status`、拥有自身数据的 `columns`、类型化 `rows`、受影响行数、插入 ID、状态标记和警告数量。

```cpp
client.async_query("SELECT 42 AS number", [](arknet::mysql::query_result result) {
    if (!result)
    {
        // 先检查 result.status.code 和 outcome_unknown，再决定后续处理。
        return;
    }
    // 先检查 field_value 当前持有的类型，再读取值。
});
```

SQL 作为 SQL 发送，不会被自动转义或改写。用户提供的值应通过预处理参数传入，不应拼接到 SQL 文本中。结果在配置的行数、列数和字节限额内完整保存在内存中，此接口不提供逐行流式交付。

## 预处理参数

`async_prepare` 返回包含 `statement` 的 `prepare_result`。将句柄和 `field_value` 数组交给 `async_execute`，参数通过二进制协议编码，不进行 SQL 插值。例如，完整程序预处理 `SELECT ? AS number, ? AS payload`，再传入 `std::int64_t(42)` 与 `std::string("ark\0net", 7)`。

检查 `statement.parameter_count()` 和操作状态。语句绑定原来的已认证会话，跨连接使用、重置或重连后使用会报告 `invalid_statement`。句柄副本共享所有权；可以显式调用 `async_close(statement, handler)`，否则最后一个句柄释放时会安排清理。关闭连接也会释放服务端语句资源。

## 事务与回调生命周期

调用 `async_begin`，按顺序提交事务命令，最后调用 `async_commit` 或 `async_rollback`。单条普通连接不会阻止其他提交者在这段序列中插入命令；应用需要管理整个事务的独占使用和顺序。

完整程序通过 promise 在主线程等待。每个回调持有共享 promise，超时或提前返回后不会再引用已经销毁的栈对象。回调运行在网络库的 IO 线程上，不得抛异常或阻塞，尤其不要在回调中等待这些 future。

如果提交失败且 `status.outcome_unknown == true`，事务仍可能已经在服务端提交，不应自动重试。详见[取消与结果未知](configuration_CN.md#取消与结果未知)。

## 连接池

完整的[连接池示例](../../../examples/mysql_pool.cpp)获取一个独占连接租约，开始事务、验证查询、提交后再归还，沿用客户端示例的端点、凭据和 TLS 环境变量。

包含 `<arknet/mysql/pool.hpp>`，配置 `pool_options.connection` 后调用 `async_acquire`。成功的 `pool::acquire_result` 包含只能移动的 `connection` 租约。通过租约执行查询、预处理、参数执行和事务操作，不会获得可绕过连接池管理的原始连接。完整事务必须使用同一个租约。

`release()` 或租约析构开始归还流程，先等待在途命令回调结束，再重置连接并恢复配置的数据库。`discard()` 禁止复用连接。已归还租约不能继续有效提交，预处理句柄也不能交给下一位借用者。配置的数据库为空时，归还的连接会被丢弃，因为重置无法清除之前使用者选择的数据库；需要复用时应指定 `pool_options.connection.database`。

超时、数据库隔离和 IO 所有权见[连接池限额与重置](configuration_CN.md#连接池限额与重置)。
