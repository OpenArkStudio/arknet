# 版本发布

## 0.2.1

完善数据库和 TCP 系列端点的关闭等待，确保已受理的原生操作、回调及其捕获在拥有者释放资源前结束。普通异步关闭与永久关闭的边界见[生命周期](../concepts/lifecycle_CN.md)。

- MySQL 连接、Redis 客户端及两者的连接池提供 `close_and_wait()`。永久关闭后停止受理新请求，等待回调完成并释放自有 IO 资源；保留的租约和语句句柄失效。
- TCP、TLS TCP、HTTP 和 WebSocket 的停止流程等待原生完成通知、Session 后续任务和回调捕获释放。关闭期间不能重新设置长定时器；显式重启和正常重连保留。
- `manual_runtime` 由库持有 context 和 work guard，不创建执行业务 handler 的工作线程，可由应用更新循环调用 `poll()`。它提供标准 Asio 执行器，支持 `awaitable` 与 `co_spawn`；公共协议端点的专用协程接口仍为 TODO。
- Redis 连接池归还任务投递失败时保留诊断，在后续操作中恢复连接额度，不重放业务命令或改写成功结果。
- 连接池关闭改为原地完成等待连接的请求，移除排空 acquire 等待请求时可能分配内存的容器移动构造；回归覆盖关闭、取消和新请求的回调重入。
- 正式回归覆盖关闭期间的并发等待、迟到连接完成、异常投递、TLS 握手及回调捕获寿命。Windows 分配失败测试覆盖 Asio 直接使用 CRT 分配的路径。

[性能报告](../performance/index_CN.md)包含本机配置矩阵、5 秒负载对照和独立的 Asio 回调／协程测量。报告记录实际测量快照 `2aa5bc0`；发行版另外修正了数据库连接池关闭时的容器排空，这项独立修复不在这些测量范围内。原始 JSON 保存在仓库外。

使用关闭等待前，应用需要停止生产者；外部 context 需要持续运行。网络关闭完成不等于业务任务已排空，也不构成插件动态卸载或部署容量保证。

## 0.2.0

此版本加入 MySQL 与 Redis 单节点原生客户端，简化网络依赖，并修复内置 IO 运行机制在进程退出时的清理顺序。arknet 保持 C++20 与 header-only。

### 数据库客户端

- **MySQL**：文本查询、类型化预处理参数与结果、显式事务、`caching_sha2_password` 与 `mysql_native_password`、验证证书的 TLS，以及带独占租约和限额的连接池。
- **Redis**：二进制安全的 RESP2/RESP3 命令、有序流水线、认证、TLS/mTLS、独立的 RESP3 推送回调，以及带独占租约和限额的连接池。
- **请求控制**：提交与结果限额、时限、取消、会话重置，以及失败命令可能已经执行的明确标志；不自动重放命令。

[数据库客户端指南](../features/database-clients_CN.md)提供完整客户端和连接池程序、配置与限制。使用时显式包含对应的 MySQL 或 Redis 公开头文件。两个客户端均支持 Boost.Asio 和 Standalone Asio。

MySQL 连接池重置后恢复配置的数据库；未配置数据库时丢弃归还的连接。预处理语句始终绑定会话。Redis 连接池在复用前重置会话，重新完成配置中的协议、认证和数据库设置。租约在未完成工作与归还处理结束前保持独占。

MySQL 认证需要 OpenSSL，即使显式禁用 TLS 也不例外；MySQL 连接接口需要启用 `ARKNET_ENABLE_SSL`。MySQL 默认要求验证证书的 TLS，不自动降级。明文 Redis 不需要 OpenSSL，安全 Redis 需要。

### 依赖与兼容性

| 配置 | 可用能力 |
| --- | --- |
| Boost.Asio + Boost.Beast，默认 | 全部受支持的传输与数据库客户端；安全能力需要 OpenSSL |
| Standalone Asio | TCP、UDP、RUDP 与数据库客户端；TLS 需要 OpenSSL |

`ARKNET_USE_BOOST_ASIO` 的默认值改为 `ON`；继续使用 Standalone Asio 时设为 `OFF`。HTTP/HTTPS 与 WebSocket/WSS 需要 Boost，内置 BHO 目录已移除。此前通过 Standalone Asio 使用这些协议的应用需要选择 Boost。`arknet/bho/` 下的头文件引用，以及对内置 BHO 类型的直接使用，需要迁移到官方 Boost API。

`arknet::arknet` 接口目标传递编译与依赖要求，不会引入 arknet 二进制库。升级已有构建或安装包前，先检查[安装与配置](../get-started/installation_CN.md)。

### 运行机制与时限修复

进程清理会在 OpenSSL 正常清理前，等待内置运行机制的回调、工作线程与 TLS 资源结束，覆盖回调释放最后一个持有者、嵌套 IO 线程池，以及受支持的直接或间接全局持有者。清理线程在第一个内置 IO 线程池构造时启动，单纯包含头文件不会启动线程。

声明全局持有者的每个翻译单元，都需要在声明前包含相应 IO 头文件。其他翻译单元中未包含该头文件、仅依赖前置声明或类型擦除的全局持有者，需要在 `main` 返回前显式清理。显式选择或预先包含的旧 Asio 线程存储配置，仍需遵守对应的退出要求，详见[进程退出](../concepts/lifecycle_CN.md#进程退出)。

数据库时限在时钟运算前检查，拒绝非正值和无法表示的时长。文档允许的单次覆盖值为零时，继续使用配置的默认值。已获准的任务在提交执行器时抛异常，会退回预留的队列额度。

MySQL 在异步投递返回前保留运行时持有者，即使完成回调销毁连接也不会提前释放运行时。
命令定时器先于对应的运行时持有者释放。独立的无效连接参数和命令拒绝销毁回归，
与正常完成、取消回调的销毁用例共同覆盖生命周期。

### 验证与范围

客户端和连接池正确性测试包含 MySQL 51 个用例、1,899 项断言，以及 Redis 35 个用例、316 项断言。独立服务端验证包含 MySQL Community Server 8.4.11 与 Redis 7.4.2。协议编解码、时钟边界和进程退出顺序另有测试；sanitizer 结果与本地 Release 测量分别记录在[测试](testing_CN.md)和[性能报告](../performance/index_CN.md)中。

MySQL 编解码测试显式包含 MSVC 断言诊断所需的标准流头文件。
聚合准入回归按编码请求大小超过字节预算，不依赖小字符串优化（SSO）或字符串容量增长方式。

MySQL 每条命令支持一个 Classic Protocol 结果，不提供多结果执行、`LOCAL INFILE`、压缩、游标或任意认证插件。Redis Cluster、Sentinel 与 Pub/Sub 仍为 TODO。公开数据库协程接口、HTTP/2、HTTP/3、named pipe、Unix domain socket 和 RPC 是[路线图](roadmap_CN.md)中的独立事项。

应用与 ark 插件回归、跨主机负载和部署耐久仍需通过[部署验收](production-readiness_CN.md)；此发行版不代表已经达到生产 PCU 容量。

## 0.1.0

首个正式发行版包含 TCP、原生 UDP、RUDP、WebSocket/WSS、HTTP/HTTPS 与路由，
以及可选的 TLS/mTLS。arknet 保持 C++20 和 header-only，支持 Standalone Asio
与 Boost.Asio 两种依赖方案。

RUDP 提供仅 ARQ、固定 Reed–Solomon FEC 与自适应 FEC 三种策略。
协商、有序交付、丢失恢复、资源限额、重连与旧 zephyr 对端互通均通过本机检查。
HTTP 单次请求提供截止时间和取消操作。

生命周期查询使用原子状态，避免与 socket、acceptor 关闭操作发生数据竞争。
正式回归用例覆盖 IO 线程启动、关闭传输期间，对客户端、服务端及保留 Session 的并发查询。

[测试记录](testing_CN.md)与 [RUDP 性能报告](../performance/rudp_CN.md)列出实际验证的
负载和源码版本。库的发行版本不代表目标部署容量：跨主机负载、生产时长耐久与
ARK 业务回归仍需完成[部署验收](production-readiness_CN.md)。
集群 10 万至 20 万 PCU 尚未实测。

`main` 分支的修改必须经过 PR，通过必需的 CI 检查并解决审查讨论，管理员也遵守
同一保护规则。合并采用 squash 与英文提交信息；当前不强制另一位维护者批准。

[GitHub 发行版](https://github.com/OpenArkStudio/arknet/releases)
