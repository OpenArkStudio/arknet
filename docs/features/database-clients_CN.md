# 数据库客户端

arknet 提供原生、header-only 的 MySQL 与 Redis 客户端，连接独立的单节点数据库服务。两者均支持 Boost.Asio 和 Standalone Asio，由库管理 IO 资源，并提供有限额的连接池，不需要 Boost.MySQL、Boost.Redis 或数据库客户端驱动。

| 能力 | MySQL | Redis |
| --- | --- | --- |
| 协议 | MySQL Classic Protocol | RESP2 或 RESP3；默认 RESP3 |
| 操作 | 文本查询、类型化预处理参数与结果、显式事务 | 二进制安全的命令与有序流水线 |
| 认证 | `caching_sha2_password`、`mysql_native_password` 及受支持的认证切换 | 密码认证与 ACL 用户名 |
| 传输安全 | 验证证书的 TLS，默认要求 TLS | 明文或验证证书的 TLS；可选 mTLS |
| 连接池 | `mysql::pool`，只能移动的独占租约 | `redis::pool`、`redis::tls_pool`，只能移动的独占租约 |
| 控制 | 时限、取消、队列与结果限额 | 时限、取消、队列、解析与回复限额 |

## 构建与运行

MySQL 认证需要 OpenSSL，即使连接显式禁用 TLS 也不例外。使用 MySQL 或安全 Redis 时启用 `ARKNET_ENABLE_SSL`；明文 Redis 不需要 OpenSSL。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_EXAMPLES=ON
cmake --build build --target arknet_mysql_client arknet_mysql_pool \
  arknet_redis_client arknet_redis_pool --config Release
```

默认选择 Boost。Standalone Asio 使用 `ARKNET_USE_BOOST_ASIO=OFF`，必要时设置 `ARKNET_ASIO_INCLUDE_DIR`。示例和基准程序是独立的 C++ 可执行文件，运行时不需要 Python 或 Ruby。

| 指南 | 完整程序 | 配置 |
| --- | --- | --- |
| [MySQL](mysql/index_CN.md) | [查询、预处理参数与事务](mysql/usage_CN.md) | [认证、限额与连接池重置](mysql/configuration_CN.md) |
| [Redis](redis/index_CN.md) | [客户端、流水线与连接池租约](redis/usage_CN.md) | [RESP 模式、TLS 与回复处理](redis/configuration_CN.md) |

示例需要显式指定服务端点，通过各自说明中的环境变量提供密码。TLS 示例保留证书链与 DNS/IP 身份验证；使用私有 CA 时需配置相应信任文件。

## 连接与连接池租约

独立的 MySQL 连接或 Redis 客户端各自管理一个 IO context 和线程。数据库连接池中的连接共享库管理的 IO 资源，连接上限不等于线程数。MySQL 连接池通过 `io_threads` 配置线程数；Redis 连接池使用一个 IO context 和线程。

`async_acquire` 返回独占租约。完整 MySQL 事务或 Redis `WATCH`/`MULTI`/`EXEC` 序列必须持有同一个租约。通过租约提交操作，随后调用 `release()` 或让租约离开作用域。需要禁止复用时调用 `discard()`。

归还租约会启动异步处理。连接池先等待已提交回调与未完成回复，再重置会话；归还期间，其他使用者不能取得此连接。

| 归还行为 | MySQL | Redis |
| --- | --- | --- |
| 重置 | `COM_RESET_CONNECTION` | `RESET`，随后重新完成配置中的协议、认证与数据库设置 |
| 数据库恢复 | 通过正确引用名称的 `USE` 恢复配置的数据库 | 重新选择配置的数据库 |
| 未配置数据库 | 丢弃归还的连接；使用者选择数据库后，重置无法恢复成未选择数据库的状态 | 恢复配置中的启动数据库 |
| 重置失败或超时 | 丢弃连接 | 丢弃连接 |

MySQL 预处理语句绑定到一个连接代次与会话，重置、重连或归还会使其失效，不能交给另一位连接使用者。普通连接的事务辅助接口不会阻止其他提交方插入命令，因此整个事务需要使用租约或由应用保证独占。

## 取消与不确定的执行结果

取消无法撤回已经发出的命令。两个客户端都不会自动重连或重放失败命令。

| 情况 | MySQL | Redis |
| --- | --- | --- |
| 取消排队中、尚未发出的命令 | 移除此请求，不消费其他命令的回复 | 不保证撤回未完成发送的命令；传输层已接受的写入仍可能执行 |
| 已发出命令被取消或超时 | 关闭会话；可能已执行时报告结果不确定 | 完成调用方回调，保留回复顺序位置与队列额度，继续接收剩余回复；超过排空时限则关闭连接 |
| 诊断字段 | `connection_result.outcome_unknown` | `result.command_may_have_run` |

MySQL 提交失败时，事务可能已经提交。失败的 Redis 流水线可能包含失败前收到的回复，后续已经发出的命令也可能执行。重试决策应依据业务语义与持久化操作标识。

Redis 服务端错误保留为 `error` 或 `bulk_error` 类型回复；`bool(result)` 检查客户端状态，还需逐项检查回复。RESP3 推送使用单独回调，不占用普通命令的回复位置。MySQL 保留可取得的服务端错误码、SQLSTATE 与消息；完整的普通 ERR 回复不同于协议边界损坏。

## 限额与回调生命周期

两个客户端都在向 IO 线程提交任务前预留有限额度。按负载配置命令数量与字节限额、结果或解析限额、连接池连接与等待数量，以及各阶段时限。这些限额不限制调用方持有的数据或进程总 RSS。

时长必须为正，且能由单调时钟表示。Redis 命令超时覆盖值，或任一连接池的取连接超时覆盖值为零时，使用配置中的默认值。MySQL 的 `query_options.timeout` 必须为正；负值或溢出覆盖值会被拒绝。操作时限包含排队等待时间。

结果自行持有值。MySQL 在配置的结果限额内缓存行，不提供逐行流式接口；Redis 流水线在回复限额内缓存结果。读取字段前先检查类型。

有效对象的完成回调在对应的库 IO 线程执行，不得阻塞或抛异常。空租约或已被移走内容的租约没有执行器，会同步拒绝并返回请求 ID 0。回调捕获的应用对象必须存活至回调完成。

`close()` 异步请求关闭，其返回不代表回调已经排空。Redis 客户端与两种连接池关闭后不再接受新工作；
独立 MySQL 连接关闭会话后可以显式重新连接。内置运行机制与全局对象的要求见
[进程退出](../concepts/lifecycle_CN.md#进程退出)。

## 支持范围与 TODO

| 范围 | 说明 |
| --- | --- |
| MySQL | 每条命令一个 Classic Protocol 结果；不提供多语句或多结果执行、`LOCAL INFILE`、压缩、游标、查询属性或任意认证插件 |
| MySQL 密码交换 | 不降级为明文完整密码，不提供 RSA 密钥交换、`mysql_clear_password`、`sha256_password` 或 MFA；不支持的交互明确失败 |
| Redis | 一个配置的端点；禁止回复或要求其他回复模型的命令会被拒绝 |
| Redis TODO | Cluster 重定向、Sentinel 发现、Pub/Sub 订阅、复制与 `MONITOR` |
| 共同 TODO | 公开协程数据库接口；按实际负载进行部署验收 |

HTTP/2、HTTP/3、named pipe、Unix domain socket 与 RPC 仍是[路线图](../project/roadmap_CN.md)中的独立事项。RESP3 推送回调不等同于 Pub/Sub 订阅接口。

## 验证与测量

独立服务端验证包含 MySQL Community Server 8.4.11 与 Redis 7.4.2。客户端和连接池用例覆盖正常操作、认证与 TLS 拒绝、畸形或不完整回复、队列饱和、取消、会话重置、租约隔离与关闭。协议编解码和 IO 运行机制另有测试覆盖。

| 客户端与连接池测试 | 用例 | 断言 |
| --- | ---: | ---: |
| MySQL | 51 | 1,899 |
| Redis | 35 | 316 |

时限测试覆盖非正时长、整数极值与时钟相加边界。正确性及 sanitizer 检查与本地 Release 性能测量分别进行；测试数量不代表吞吐量或容量。

MySQL 在异步投递返回前保留运行时持有者，允许完成回调销毁连接。
独立的无效连接参数和命令拒绝回归覆盖这些快速完成路径。
MySQL 连接与连接池测试在两种 Asio 依赖下通过 TSan，并在 Boost.Asio 下通过 ASan+UBSan；
[测试记录](../project/testing_CN.md)列出范围与计数。

[MySQL](mysql/performance_CN.md) 与 [Redis](redis/performance_CN.md) 性能页提供可编译的基准程序、负载维度和报告入口。报告需记录机器与服务端配置、包体、排队或流水线深度、连接数、重复轮次、每秒操作数、RT 分位数和 CPU 占用比例。容量需经过[部署验收](../project/production-readiness_CN.md)。

arknet 负责协议 IO、认证、回复关联、连接池与关闭；ark 数据库插件负责框架配置与模块集成；应用负责事务边界、业务结果与重试决策。客户端发布后，ark 插件仍需通过应用回归测试才能确认兼容。
