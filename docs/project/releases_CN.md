# 版本发布

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
