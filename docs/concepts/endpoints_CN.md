# 客户端、服务端与 Session

客户端管理一个对端。服务端拥有监听器并管理 Session。Session 保存对端的
状态、回调、定时器与发送队列。

| 对象 | 职责 | 生命周期 |
| --- | --- | --- |
| Client | 解析、连接、发送、接收与可选重连 | 由应用持有，停止完成前保持存活 |
| Server | 监听、创建 Session 与协调关闭 | 由应用持有，先于所属 IO 资源停止 |
| TCP 派生 Session | 一个已接受的 TCP、HTTP、WebSocket 或 TLS 连接 | 由服务端管理，未完成操作可能继续持有它 |
| UDP Session | 来源 IP 与端口对应的状态 | 共用服务端 socket 与串行接收通道 |
| UDP cast | 向显式端点发送和接收原生数据报 | 由应用持有，不执行远端握手 |

UDP Session 不认证对端，也不会把数据报变成可靠连接。TCP 交付同样不确认
业务处理完成。身份、请求 ID、应答和重试策略由应用定义。

CRTP `*_t<Derived>` 端点类型允许添加自定义状态，同时保留传输组件。
在状态所属执行通道访问它，跨通道访问时同步。见[架构与扩展](../project/architecture_CN.md)。

[生命周期](lifecycle_CN.md) · [IO 模型](io-models_CN.md) · [发送](sending_CN.md) · [公开类型](../reference/api_CN.md)
