# 核心概念

这些概念说明协议模块共用的约定，帮助确定连接的所有者、回调的执行位置
以及数据的有效期。

- [客户端、服务端与 Session](endpoints_CN.md)：对象职责与对端身份。
- [IO 模型与协程](io-models_CN.md)：运行线程、strand 与业务选型。
- [生命周期与执行上下文](lifecycle_CN.md)：启动、取消与安全销毁。
- [发送、所有权与背压](sending_CN.md)：入队、完成与借用数据。

具体配置任务见[使用指南](../guides/index_CN.md)。
