# Redis 配置与限额

## 连接与协议

调用 `async_connect` 前配置 `redis::connect_options`：`host`、`port`、`username`、`password`、`client_name`、`database` 与 `protocol`。域名由网络库解析。Redis 端口需要明确提供，不采用 HTTP/HTTPS 的默认端口规则。

默认 `protocol_version::resp3`，需要成功收到 `HELLO 3` 回复；协商失败时不会静默回退或重放命令。需要 RESP2 认证和启动流程时，显式选择 `protocol_version::resp2`。只提供密码时认证默认用户；显式用户名也允许空密码，前提是服务端 ACL 允许。有其他请求在途时，不要通过原始 `HELLO` 命令切换协议。

## 命令与回复类型

`command` 是 `std::vector<std::string>`，每个元素是一项二进制安全的参数。`async_pipeline` 保持命令顺序，一次返回 `result`，其中 `replies` 拥有自身数据。RESP3 推送由 `set_push_handler` 单独接收，不占流水线的回复位置；没有注册处理器的推送会被忽略。

| `value_type` | 数据成员 |
| --- | --- |
| `simple_string`、`bulk_string`、`error`、`bulk_error`、`big_number`、`verbatim_string` | `text` |
| `integer`、`real`、`boolean` | 对应同名成员 |
| `array`、`set`、`push` | `elements` |
| `map`、`attribute` | `elements` 中交替排列键和值，保留键的类型 |
| `null` | 无数据 |

`attributes` 保存附着在该值上的 RESP3 属性。读取数据前先检查类型。服务端错误是类型化回复，不是传输失败：`bool(result)` 检查 `client_errc`，应用仍需检查每一项回复。流水线失败时，结果可能保留失败前已经收到的回复。

## 限额与超时

构造客户端时配置 `redis::client_options`；连接池使用 `pool_options.client_configuration`。

| 选项 | 默认值 | 范围 |
| --- | --- | --- |
| `max_pending_commands` | 1024 | 占用队列的命令数，包含已发送、仍待排空回复的请求 |
| `max_pending_bytes` | 16 MiB | 占用队列的命令编码字节数 |
| `max_pipeline_response_bytes` | 64 MiB | 单个流水线保留的回复值，包含值对象开销 |
| `connect_timeout` | 5 秒 | 从提交开始，包含解析、传输连接与 Redis 握手 |
| `command_timeout` | 5 秒 | 从命令提交到完整回复交付 |
| `drain_timeout` | 5 秒 | 已发送请求取消或超时后，等待剩余回复的时限 |

`response_limits` 还限制输入缓冲、单个 RESP 消息、bulk 值、单行、嵌套值数量和深度。默认缓冲／消息／bulk 上限均为 64 MiB，单行为 64 KiB，值数量为 1,048,576，深度为 64。限额与超时必须为正，配置的深度不能超过 128。这些是协议与队列限额，不代表进程常驻内存上限，也不能代替应用层背压。

`async_execute` 或 `async_pipeline` 的可选正数超时参数会覆盖 `command_timeout`。返回的 `request_id` 可交给 `cancel`。队列满时，通过完成回调报告 `client_errc::queue_full`，不会静默丢弃命令。

### 写入结果未知

取消会完成调用者的请求，但无法撤回传输层已经接受的写入。失败结果中的 `command_may_have_run == true` 表示服务端可能已经执行发出的命令，只是回复未交付。不要自动重试非幂等写操作。

已经发送的请求取消或超时后，仍保留有序回复位置及队列配额，直到剩余回复排空，避免旧回复被分配给后来的命令。排空超过 `drain_timeout` 时关闭连接，让在途请求失败。失败命令不会自动重放。

## 连接租约与状态隔离

`pool_options` 默认最多 8 条连接、1024 个待获取请求，获取超时与重置超时均为 5 秒。`max_waiters` 包含正在等待新连接建连完成的获取请求。`async_acquire` 支持覆盖超时，并返回可传给 `pool.cancel(id)` 的 ID。租约提供命令提交与取消接口，不暴露可被绕过管理的原始客户端。

完整事务或其他连接级操作应始终持有一个租约。归还时先等待已提交回调与有序回复，再执行 [`RESET`](https://redis.io/docs/latest/commands/reset/)，重新完成配置中的协议、认证和数据库设置，隔离未结束事务、监视的键、数据库选择和认证变化。重置时限包含等待先前回复的时间。重置失败、服务端不支持或连接不健康时，丢弃连接，不在替换连接上重放命令。

重复调用 `release()` 无害。已归还租约上的提交会报告 `closed`；空租约或已移动租约没有执行器，会同步拒绝并返回请求 ID 0。`discard()` 归还租约但不允许复用连接。连接池共用库管理的 IO 资源，`max_connections` 不是线程数。

## TLS 选项

启用 `ARKNET_ENABLE_SSL=ON` 后，使用 `redis::tls_client` 或 `redis::tls_pool`：

```cpp
arknet::redis::client_options options;
options.tls_certificate_authority = "/path/to/ca.pem";
options.tls_certificate_file = "/path/to/client.pem";
options.tls_private_key_file = "/path/to/client-key.pem";
arknet::redis::tls_client client(options);
```

单向 TLS 不要求客户端证书和私钥；服务端要求 mTLS 时需要提供。加密私钥可通过 `tls_private_key_password` 配置密码。连接池通过 `client_configuration` 使用这些字段；若指定 `pool_options.certificate_authority`，则采用该 CA 文件。证书链与 DNS/IP 身份验证保持启用。验证身份由连接的主机名决定，与 Redis 客户端名称无关。

### 服务端 TLS 重试问题

本机 Redis 7.4.2 配合 OpenSSL 3.6.4，在 16 KiB ECHO、每批 64 条命令、每连接 4 批在途的负载下出现服务端主动断开，日志为 `SSL routines::bad length`，客户端收到 `Broken pipe` 或连接重置。官方 `redis-benchmark` 在同一服务端、相同在途命令量下也出现该错误。其服务端 `src/tls.c` 写入路径与 Valkey [上游修复说明](https://github.com/valkey-io/valkey/pull/1737)中的 TLS 重试长度问题一致；这是源码分析，未通过仅移植该补丁的对照证明唯一根因。

包含上游修复的 Valkey 8.0.11 在原负载下通过两套客户端与官方工具的对照。性能报告使用该服务端，并明确标注版本。这不是 Redis 报文大小上限。

遇到这类断开时先检查服务端日志和修复版本。客户端不会通过关闭证书验证、静默重连或重放写命令掩盖服务端错误。

## 执行与关闭

网络库管理 IO context、线程和传输对象的生命周期。独立客户端使用一个 IO context 和线程；连接池使用一个由内部客户端共用的 context 和线程。已受理命令、连接完成和推送回调运行在对应 IO 线程上，回调不得抛异常或阻塞。空租约和永久关闭客户端的拒绝在调用线程同步完成。

`close()` 先原子标记关闭，再调度终止流程。在途操作恰好完成一次，但 `close()` 返回**不代表**回调或传输对象回收已经排空。析构也可能请求相同的异步关闭，包括在回调中析构。应用需要管理回调捕获对象的生命周期；请求超时或客户端析构不会让悬空引用变得安全。

如果 `close()` 在关闭流程开始前投递失败，会恢复请求标记，允许之后重试；已经开始的关闭仍保持终止状态。

独立客户端可在非 IO 线程调用 `close_and_wait()`：永久关闭准入，等待此前受理的 handler 执行及其捕获释放，等待自有工作线程退出，并在销毁传输对象、TLS 对象和定时器后返回成功。在自身 IO 线程、提交或清理流程的重入调用中，以及连接池内部借用运行时的客户端上，返回 false 并设置 `operation_not_supported`，不发起永久关闭。等待时不要持有回调需要的锁。关闭流程失败可能抛出异常。

永久准入门关闭后，连接和命令提交会在调用线程同步回调 `client_errc::closed`，命令返回 ID 0；取消、推送回调注册和普通 `close()` 不再提交任务。这些后续同步拒绝不属于此前已受理操作集合，其 handler 可以向调用者传播异常。应用仍需自行停止并等待提交线程退出，再释放共享应用代码。此方法不提供连接池、进程清理线程或动态库卸载屏障。

连接不会自动重连，终止关闭后需创建新客户端。连接池的 `close()` 同样终止待获取请求和内部连接，不提供回调排空屏障。
