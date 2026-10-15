# MySQL 配置与限额

## 连接与 TLS

`connect_options` 配置 `host`、数值 `port`（默认 3306）、`username`、`password`、可选 `database` 和默认 10 秒的 `timeout`。连接时限覆盖 DNS 解析、建连、TLS 协商和 MySQL 认证。

默认 `tls_mode::required`。客户端先发送 MySQL SSLRequest，再启动 TLS，验证证书信任和 DNS/IP 身份；服务端不支持 TLS 时失败。`ca_file` 指定私有 CA 文件，否则使用默认信任路径。`server_name` 默认等于 `host`；覆盖它会改变验证的身份和 DNS SNI，不会改变连接地址，也不会关闭对端验证。

`tls_mode::disabled` 是显式明文选项，不允许明文完整密码认证；没有经过验证的 TLS 时，`caching_sha2_password` 完整认证请求会失败。认证哈希仍需要 OpenSSL。客户端不会自动降级 TLS，也不提供 RSA 公钥密码交换。

支持 `caching_sha2_password` 和 `mysql_native_password`，以及服务端要求的受支持认证切换。未知插件和不支持的认证交互会明确失败，不提供 `mysql_clear_password`、`sha256_password`、MFA 或任意插件加载。

## 查询与队列限额

| 选项 | 默认值 | 范围 |
| --- | --- | --- |
| `connect_options.max_message_size` | 64 MiB | 单个逻辑 Classic Protocol 消息 |
| `connect_options.max_pending_commands` | 64 | 一条执行中请求之外，额外待执行的请求数 |
| `connect_options.max_pending_bytes` | 64 MiB | 执行中、排队及尚未投递到 IO 线程的请求和参数存储 |
| `query_options.timeout` | 10 秒 | 从提交到结果完成，包含排队时间 |
| `query_options.max_rows` | 100000 | 单条命令保留的结果行数 |
| `query_options.max_columns` | 4096 | 结果列数 |
| `query_options.max_result_bytes` | 64 MiB | 累计结果处理预算 |

命令投递到 IO 线程前，先预留请求数量和实际保留的输入容量。拒绝时释放大块数据，通过完成回调报告 `queue_full`；IO 线程被阻塞时，也不能在计数器之外无限积压请求。这些限额不约束调用者自行持有的数据，也不代表进程内存总上限。

请求按 FIFO 执行，同一条连接同时只有一条命令在传输。查询、预处理、执行、关闭语句和事务辅助接口均接受 `query_options`。结果完整保存在内存中。协议错误、不支持的后续结果、结果超限或未完成响应会关闭连接，避免在报文边界不确定时继续复用。

## 字段类型

`field_value` 拥有以下类型之一：表示 SQL NULL 的 `std::monostate`、有符号或无符号 64 位整数、`double`、`std::string`、`date_value`、`datetime_value` 或 `time_value`。文本查询结果和预处理二进制结果都依据列元数据解码。DECIMAL、BIT、JSON 和字节／字符串字段保留原字节，不隐式损失精度或进行字符集转换。时间值保留服务端各组成部分，不自动转换时区。

预处理参数同样使用这些类型，通过二进制编码发送，不进行 SQL 转义。不支持的字段编码和非法取值范围会明确失败。读取前先检查 variant 当前持有的类型。

## 取消与结果未知

每条命令返回可传给 `cancel(id)` 的 `request_id`。取消或超时发生在排队阶段时，仅移除该请求，不影响当前命令的回复。已经发送的执行中命令取消或超时，会关闭会话，避免剩余回复被误认为后续命令的回复。

失败命令可能已经执行时，`connection_result.outcome_unknown` 为 true。例如等待 `COMMIT` 回复期间连接中断，事务可能已经提交，只是客户端没有收到成功回复。尚未发送的排队命令会失败，不会重放。应用需要依据操作语义和持久化业务标识决定恢复方式，客户端不会隐式重试写入。

`code` 区分本地、协议、传输和服务端错误；`transport_error`、`server_code`、`sql_state` 与 `message` 保留可用诊断。完整、正常的服务端 ERR 回复可能允许继续复用连接；非法或不完整响应会关闭连接。并非所有服务端错误都表示写入结果未知。

## 连接池限额与重置

`pool_options` 提供 `max_connections`（8）、`max_waiters`（1024）、`io_threads`（1）、`acquire_timeout`（5 秒）、`reset_timeout`（5 秒）及 `connection` 配置。IO context 和线程属于网络库，连接共用池内运行资源；提高连接上限不会隐式为每条连接创建线程。

`async_acquire` 返回可传给 `pool.cancel(id)` 的获取请求 ID，并支持覆盖超时。租约独占一条连接，命令 ID 通过 `lease.cancel(id)` 取消。获取请求限额包含正在等待和建连的请求。替换连接时不重放命令或事务。

归还时限从 `release()` 或租约析构请求归还时开始，包含在途回调排空、COM_RESET_CONNECTION 和数据库恢复。MySQL 重置不会恢复所选数据库，连接池会通过正确引用名称的 `USE` 切回配置的数据库，再允许复用。若配置的数据库为空，每次归还都丢弃连接，因为使用者选择数据库后，重置无法恢复成「未选择数据库」。重置、恢复失败，超过时限或连接不健康时也会丢弃。

租约析构和 `release()` 只发起归还，不会阻塞调用线程；连接会在已提交回调返回后才允许复用。该会话的预处理语句在重置或丢弃后失效，不能交给下一位借用者。空租约或已移动租约没有执行器，会同步拒绝并返回请求 ID 0。连接池的永久准入门仍开放时，仍持有控制信息的已归还租约在库管理的 IO 线程上完成拒绝；永久准入门关闭后，保留租约在调用线程同步拒绝，请求 ID 为 0。`close()` 异步终止连接池，返回不代表回调已经排空。

## 语句、重置与生命周期

`prepared_statement` 是可复制、共享所有权的不透明句柄，绑定一代连接和具体会话。副本共用关闭状态。`async_close` 发送没有回复的 COM_STMT_CLOSE；完成成功表示客户端写入完成，不是服务端确认。最后一个句柄释放时，若会话仍有效，会安排相同的资源清理。

`async_reset` 执行 COM_RESET_CONNECTION，成功后使现有语句句柄失效，并重置服务端会话与事务状态。不要跨重置、重连保留语句，也不要在其他连接上使用。普通连接的事务辅助接口不提供独占事务所有权。

每条独立连接使用由网络库管理的一个 IO context 和线程。已受理操作的回调在该 IO 线程上恰好执行一次，不得抛异常或阻塞。空租约的拒绝，以及永久准入门关闭后的请求拒绝，会在调用线程同步调用 handler；这些拒绝回调可以向调用方传播异常。`close()` 异步请求取消，返回不代表回调已经排空。回调捕获的应用对象需要存活到回调完成；支持在回调中析构连接，无需调用者管理 IO 线程。客户端不会自动重连或重放命令。
