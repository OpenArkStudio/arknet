# 版本发布

## 0.2.1

此版本完善数据库客户端与 TCP 系列协议的关闭等待，并提供由应用驱动的 IO 执行方式。

### 新增能力

- **数据库关闭等待**：MySQL 连接、Redis 客户端及两者的连接池提供 `close_and_wait()`。永久关闭后停止受理新请求，等待已受理操作、回调与自有 IO 资源释放；保留的租约和语句句柄失效。
- **应用驱动执行**：`manual_runtime` 允许应用在更新循环中调用 `poll()` 驱动 IO，不创建执行业务回调的工作线程。标准 Asio 执行器支持 `awaitable` 与 `co_spawn`；公共协议的专用协程接口仍为 TODO。

### 修复

- TCP、TLS TCP、HTTP 和 WebSocket 关闭时等待未完成的 IO 与回调，避免端点资源在仍被使用时提前释放。关闭期间不能重新设置长定时器；显式重启和正常重连保留。
- 数据库连接池关闭时完成等待连接的请求，不依赖容器分配内存，并支持回调中再次关闭或取消。Redis 连接池归还任务投递失败时保留诊断并恢复连接额度，不重放命令或改写成功结果。

### 升级注意

等待关闭前，需要停止应用生产者，并保持外部 context 运行至关闭完成。网络关闭不会排空业务任务，也不保证动态插件可以安全卸载。详见[生命周期](../concepts/lifecycle_CN.md)与[数据库关闭](../features/database-clients_CN.md#限额与回调生命周期)。

[5 秒负载对照](../performance/steady-workloads_CN.md)中，4 KiB WebSocket 的吞吐中位数下降 6.51%，P99 上升 13.29%；五轮吞吐范围重叠，原因尚未确认。测量使用快照 `2aa5bc0`，早于发行版最后的连接池关闭修复。详细[验证记录](testing_CN.md)、[性能报告](../performance/index_CN.md)与 Release 附件保留实际源码标识；部署容量尚未确认。

## 0.2.0

此版本加入 MySQL 与 Redis 单节点原生客户端，将 Boost 设为默认网络依赖，并完善进程退出清理。arknet 保持 C++20 与 header-only。

### 新增能力

- **MySQL**：文本查询、类型化预处理参数与结果、显式事务、`caching_sha2_password` 与 `mysql_native_password`、验证证书的 TLS，以及带独占租约和限额的连接池。
- **Redis**：二进制安全的 RESP2/RESP3 命令、有序流水线、认证、TLS/mTLS、独立的 RESP3 推送回调，以及带独占租约和限额的连接池。
- **请求控制**：队列与结果限额、时限、取消、会话重置，以及失败命令可能已经执行的明确标志；不自动重放命令。

两个客户端均支持 Boost.Asio 和 Standalone Asio。[数据库客户端指南](../features/database-clients_CN.md)提供完整程序、配置与连接池复用规则。MySQL 连接池重置后恢复配置的数据库；未配置数据库时丢弃归还的连接，预处理语句始终绑定会话。Redis 连接池复用前恢复协议、认证和数据库设置。租约在未完成工作与归还处理结束前保持独占。

### 修复

- 内置 IO 运行机制在进程退出时，先等待回调、工作线程与 TLS 资源结束，再执行 OpenSSL 的正常清理，覆盖受支持的回调与全局持有者生命周期。清理线程在第一个内置 IO 线程池构造时启动，单纯包含头文件不会启动线程。
- 数据库请求在时钟运算前拒绝无效或无法表示的时限，投递异常时退回队列额度。文档允许的单次覆盖值为零时，继续使用配置默认值。
- MySQL 在异步投递返回前保留运行时，并先释放命令定时器、再释放运行时；完成回调销毁连接时也遵守此顺序。

### 升级注意

| 依赖配置 | 可用能力 |
| --- | --- |
| Boost.Asio + Boost.Beast，默认 | 全部受支持的传输与数据库客户端；安全能力需要 OpenSSL |
| Standalone Asio | TCP、UDP、RUDP 与数据库客户端；TLS 需要 OpenSSL |

`ARKNET_USE_BOOST_ASIO` 的默认值改为 `ON`；继续使用 Standalone Asio 时设为 `OFF`。HTTP/HTTPS 与 WebSocket/WSS 需要 Boost。内置 BHO 目录已移除，`arknet/bho/` 下的头文件引用与内置 BHO 类型需要改为官方 Boost API。`arknet::arknet` 接口目标传递头文件、设置与依赖，不引入 arknet 二进制库。升级时请检查[安装与配置](../get-started/installation_CN.md)。

MySQL 认证需要 OpenSSL，即使禁用 TLS 也不例外；连接接口需要启用 `ARKNET_ENABLE_SSL`。MySQL 默认要求验证证书的 TLS，不自动降级。明文 Redis 不需要 OpenSSL，安全 Redis 需要。

声明全局 IO 持有者的每个翻译单元，都需要在声明前包含相应 arknet IO 头文件。未包含该头文件的持有者，包括前置声明或类型擦除的全局对象，需要在 `main` 返回前显式清理。旧 Asio 线程存储配置仍需遵守对应要求，详见[进程退出](../concepts/lifecycle_CN.md#进程退出)。

MySQL 每条命令支持一个 Classic Protocol 结果；多结果执行、`LOCAL INFILE`、压缩、游标与任意认证插件不在此版本范围内。Redis Cluster、Sentinel、Pub/Sub、数据库专用协程接口及其他规划协议见[路线图](roadmap_CN.md)。

独立 MySQL、Redis 服务端检查与 Sanitizer 覆盖见[测试](testing_CN.md)，本机测量见[性能报告](../performance/index_CN.md)。应用与 ark 插件回归、跨主机负载和部署耐久仍需通过[部署验收](production-readiness_CN.md)；此版本不代表已经达到生产 PCU 容量。

## 0.1.0

首个正式发行版提供 C++20 header-only 网络库，包含客户端与服务端 API，支持 CRTP 扩展与组件组合。

### 新增能力

- TCP、原生 UDP、RUDP、WebSocket/WSS、HTTP/HTTPS 与路由，可选的 TLS/mTLS，以及 IPv4/IPv6。
- RUDP 提供仅 ARQ、固定 Reed–Solomon FEC 与自适应 FEC 三种策略。本机检查覆盖协商、有序交付、丢失恢复、资源限额、重连与旧 zephyr 对端互通。
- 自动重连、定时器与 `post`、发送限额、内置或外部 IO context，以及 HTTP 单次请求的时限与取消。

### 修复

生命周期查询使用原子状态，避免与 socket、acceptor 关闭操作发生数据竞争，也覆盖保留 Session 的并发查询。

### 升级注意

此版本默认使用 Standalone Asio，同时支持 Boost.Asio；默认依赖在 0.2.0 中改为 Boost。

[测试](testing_CN.md)与 [RUDP 性能](../performance/rudp_CN.md)列出实际验证的负载和源码版本。跨主机负载、生产时长耐久与 ark 业务回归仍需完成[部署验收](production-readiness_CN.md)。集群 10 万至 20 万 PCU 尚未实测。

[GitHub 发行版](https://github.com/OpenArkStudio/arknet/releases)
