# 版本发布

## 下一版本发布准备

候选版本尚未确定版本号与发布日期。已发布版本仍为 `0.1.0`；下面列出发布条件，
不表示这些新增能力已经可用。

候选范围包含 MySQL 与 Redis 单节点原生客户端，以及默认使用 Boost.Asio／Beast、移除 BHO
的依赖简化。Standalone Asio 保留 TCP、UDP、RUDP 与 TLS。这些修改合并并通过验证前，
继续以[安装文档](../get-started/installation_CN.md)中的默认值与可用协议为准。

| 发布条件 | 需要完成的内容 |
| --- | --- |
| MySQL 客户端 | 验收[查询、预处理、事务范围](../features/database-clients_CN.md)，以及认证／TLS、连接池、限额、取消和独立服务端互通 |
| Redis 客户端 | 验收 RESP2／RESP3 命令与流水线，以及认证／TLS、连接池、限额、取消和独立服务端上的回复对应关系 |
| 依赖调整 | 完成 [PR #6](https://github.com/OpenArkStudio/arknet/pull/6) 中待处理的 Windows 失败诊断，整合当前 IO 生命周期修复，再验证协议、依赖方案、TLS 构建组合及安装后的调用方 |
| 验证记录 | 记录准确源码版本、服务端与机器配置、正确性与异常路径结果、并发检查、可复现的本地性能测量 |
| 公开文档 | 提供可编译的客户端程序、经过验证的 API 参考、支持的服务端版本与认证方式，以及中英文限制说明 |

仅完成协议编解码不能替代客户端发布验收。候选修改经过 PR 接受并通过必需检查后，
再选择版本号、更新版本信息并发布最终说明。这里不承诺部署容量；
[生产验收](production-readiness_CN.md)仍需结合实际负载完成。

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
