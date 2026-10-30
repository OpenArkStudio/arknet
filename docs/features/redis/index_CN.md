# Redis

arknet 提供原生、header-only 的单节点 Redis 客户端和连接池。命令与回复使用 arknet 类型，不需要 Boost.Redis 或其他 Redis 驱动。支持 Boost.Asio 与 Standalone Asio；TLS 通过 `ARKNET_ENABLE_SSL` 启用，需要 OpenSSL。

| 分类 | 接口与行为 |
| --- | --- |
| 连接 | `redis::client`、`redis::tls_client`，显式指定主机与端口，支持认证和数据库选择 |
| 协议 | RESP2 与 RESP3，默认通过 `HELLO` 协商 RESP3 |
| 命令 | 二进制安全的参数数组、单条命令和有序流水线 |
| 回复 | 拥有自身数据的类型化值、嵌套集合、服务端错误和 RESP3 属性 |
| 推送 | 独立推送回调，不占用命令的回复位置 |
| 连接池 | `redis::pool`、`redis::tls_pool`，连接数和待获取请求有上限，使用只能移动的独占租约 |
| 失败处理 | 超时、取消、队列限额和明确的写入结果未知标记，不自动重放命令 |
| TLS | 验证证书与主机名，可通过客户端证书启用 mTLS |

## 开始使用

<CardGrid>

<LinkCard title="客户端与连接池示例" href="usage_CN.md" description="编译完整程序，连接自己的 Redis 端点。" />
<LinkCard title="配置与限额" href="configuration_CN.md" description="认证、回复类型、取消与租约隔离。" />
<LinkCard title="性能" href="performance_CN.md" description="测试维度、指标单位与独立 C++ 基准程序。" />

</CardGrid>

IO context 和线程由网络库管理。独立客户端使用一个 context 和线程；连接池中的客户端共用该连接池的 context 和线程。应用提交命令，无需自行启动或停止这些 context。

## 范围

此模块面向一个配置明确的 Redis 端点，不提供 Redis Cluster 重定向、Sentinel 发现、Pub/Sub 订阅、复制或 `MONITOR`。关闭回复或需要其他回复模型的命令会被拒绝。RESP3 推送回调与 Pub/Sub 订阅接口是不同能力。

连接不会自动重连，命令不会自动重放。`close()` 是终止性的异步关闭请求；重新连接需要创建新的客户端或连接池。接入应用关闭流程前，请阅读[执行与关闭](configuration_CN.md#执行与关闭)。
