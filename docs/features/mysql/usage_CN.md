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

完整程序通过 promise 在主线程等待。每个回调持有共享 promise，超时或提前返回后不会再引用已经销毁的栈对象。已受理操作的回调运行在网络库的 IO 线程上，不得抛异常或阻塞，尤其不要在回调中等待这些 future。

如果提交失败且 `status.outcome_unknown == true`，事务仍可能已经在服务端提交，不应自动重试。详见[取消与结果未知](configuration_CN.md#取消与结果未知)。

## 独立连接的永久关闭

`close()` 取消当前会话，之后仍可调用 `async_connect`。对于独立构造的 `connection`，
不再使用时可以从非 IO 的拥有者线程调用 `close_and_wait()`：

```cpp
// 先停止应用提交方。不要持有回调执行所需的锁。
if (!client.close_and_wait())
{
    // 从自身 IO 线程调用时，get_last_error() 为 operation_not_supported。
}
```

成功返回表示已接受的操作各自完成一次，库持有的 handler 捕获已析构，保留语句的清理回调已禁用，
该连接自有的 IO 工作线程已退出并 join。并发等待者观察同一个完成状态。普通 `close()` 的语义不变。

永久提交入口会先封闭，再等待。此后的 `async_*` 在调用线程同步调用拒绝回调，状态为 `not_connected`。
返回请求 ID 的命令接口返回 0；`async_connect` 返回 `void`，并同步调用拒绝回调。这些拒绝回调可以向调用方传播异常。此后的 `cancel()` 和 `close()` 不做操作。
终止前接受的回调仍在 IO 线程执行，不得抛异常。关闭过程中发生的内存分配或系统异常会向调用方传播，
不表示关闭屏障已经完成。

从自身 IO 线程、池内子连接，或正在发布请求、执行终止的重入调用中等待，会返回 false，
并设置 `operation_not_supported`，不会开始永久终止。也不得在其他网络库 IO 线程中等待；
这一项是调用约定，当前不会全局检测所有 IO 线程。回调不返回时，关闭可能一直等待，没有超时后安全销毁的保证。

此操作不会停止其他连接或连接池，也不构成进程清理线程或动态库卸载屏障。
卸载代码前，宿主仍须停止并等待应用调用方，并销毁其持有的对象。

## 连接池

完整的[连接池示例](../../../examples/mysql_pool.cpp)获取一个独占连接租约，开始事务、验证查询、提交后再归还，沿用客户端示例的端点、凭据和 TLS 环境变量。

包含 `<arknet/mysql/pool.hpp>`，配置 `pool_options.connection` 后调用 `async_acquire`。成功的 `pool::acquire_result` 包含只能移动的 `connection` 租约。通过租约执行查询、预处理、参数执行和事务操作，不会获得可绕过连接池管理的原始连接。完整事务必须使用同一个租约。

`release()` 或租约析构开始归还流程，先等待在途命令回调结束，再重置连接并恢复配置的数据库。`discard()` 禁止复用连接。已归还租约不能继续有效提交，预处理句柄也不能交给下一位借用者。配置的数据库为空时，归还的连接会被丢弃，因为重置无法清除之前使用者选择的数据库；需要复用时应指定 `pool_options.connection.database`。

超时、数据库隔离和 IO 所有权见[连接池限额与重置](configuration_CN.md#连接池限额与重置)。


### 连接池永久关闭

普通 `pool::close()` 异步请求取消。需要等待连接池拥有的 IO 工作结束时，先停止应用侧生产者，
再从非库 IO 线程的拥有者线程调用 `pool::close_and_wait()`：

```cpp
if (!connections.close_and_wait())
{
    // 自身 IO 线程或重入调用：operation_not_supported。
}
```

成功表示已受理的获取连接和租约操作都已交付终态回调，回调捕获对象已释放，池拥有的 IO 线程均已退出并 join。
此前已因丢弃而移出连接列表的子连接也在清理范围内。仍存活的租约和预处理语句会失效，不再保留子连接的
executor、定时器、SSL stream 或自动清理 callable。单个租约不会停止共享 runtime，丢弃它不会影响其他租约。

永久关闭入口覆盖获取连接、取消、租约提交和归还。此后的 `async_acquire` 和租约 `async_*` 在调用线程同步返回
`not_connected`，请求 ID 为 0；拒绝回调可以向调用方抛出异常。此后的取消和归还不再投递工作。
并发等待者观察同一个完成状态；关闭异常会向调用方传播，不表示已经完成清理。已受理的 IO 回调必须正常返回且不得抛出异常。

在池自身 IO 线程调用，或在投递和终结过程中重入调用，会返回 false 并设置 `operation_not_supported`。
也不要从其他库 IO 线程等待，不要持有回调需要的锁，或让回调等待当前拥有者线程。超时不能代表安全清理已经完成。
卸载代码前，宿主仍需停止应用调用方并销毁其对象；该接口不是进程清理线程或动态库卸载屏障。

### 归还投递诊断

`pool::diagnostics()` 返回线程安全、有界的 `pool_diagnostics` 快照：

- `return_submission_failures`：内部归还或回收关闭投递失败的饱和计数。
- `last_return_submission_exception`：最后一次异常；没有发生此类失败时为空。

投递失败的会话不再参与复用。后续获取连接、获取超时或取消，以及已排队的归还处理，可以丢弃该会话并恢复容量。
如果关闭投递再次失败，会话仍保留在索引中且不可用，等待后续调度处理。获取连接的原期限保持不变；没有后续调度活动时不保证立即回收。

这些失败不改写已交付的 SQL 结果，也不会重放语句。该处理覆盖内部归还和回收关闭的投递，不保证任意分配失败或用户回调抛出异常后的进度。
