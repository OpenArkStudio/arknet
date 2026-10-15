# MySQL

arknet 提供原生、header-only 的单节点 MySQL Classic Protocol 客户端，通过自身 IO 运行机制实现报文拆包、认证、查询和预处理命令，不需要 Boost.MySQL 或 MySQL 客户端驱动。

| 分类 | 接口与行为 |
| --- | --- |
| 连接 | `mysql::connection`，支持域名或 IP 端点、数据库和凭据配置 |
| TLS | 默认要求 TLS，验证证书链与 DNS/IP 身份 |
| 认证 | `caching_sha2_password`、`mysql_native_password` 及受支持的认证切换 |
| 查询 | FIFO 文本查询，列元数据和类型化结果行拥有自身数据 |
| 预处理 | 类型化二进制参数和结果，语句句柄绑定具体会话 |
| 事务 | 在同一条连接上显式开始、提交和回滚 |
| 连接池 | 有限额的 `mysql::pool`，只能移动的独占租约，复用前重置会话 |
| 控制 | 队列与结果限额、超时、取消和会话重置 |
| 失败处理 | 服务端错误码与 SQLSTATE，明确标记结果未知，不重放命令 |

## 开始使用

- [查询、预处理与事务示例](usage_CN.md)。
- [配置与限额](configuration_CN.md)。
- [性能测量方法](performance_CN.md)。

连接 API 需要启用 `ARKNET_ENABLE_SSL`。即便某条连接显式禁用 TLS，认证哈希仍需要 OpenSSL。支持 Boost.Asio 与 Standalone Asio，IO context 和线程由网络库管理。

## 范围

每条命令处理一个 Classic Protocol 结果，不协商多语句或多结果执行、`LOCAL INFILE`、压缩、游标分批获取、query attributes、可选结果元数据或任意认证插件。不受支持的协议交互会明确失败，不会尝试在回复边界不确定时继续复用连接。

真实服务端验证包含 MySQL Community Server 8.4.11。协议测试覆盖报文分片、类型化值、限额与取消；部署容量和性能需要单独在本地测量，边界见[性能](performance_CN.md)。
