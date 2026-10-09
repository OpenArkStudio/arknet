# 路线图与 TODO

当前实现包含 TCP、UDP/cast、WebSocket、HTTP/1.0 与 HTTP/1.1、路由、
定时器／重连，以及可选的 TLS/HTTPS/WSS/mTLS。下面的计划尚未提供占位 API，
也没有承诺发布日期。

已实现能力的吞吐量、p99 图表、模型对照和测试机器配置见
[本地性能报告](performance/overview_CN.md)。

## 协议与传输 TODO

| 能力 | 状态 | 实现范围与验收 |
| --- | --- | --- |
| HTTP/2 | TODO，依赖选型待定 | client/server/session、TLS ALPN `h2`、h2c prior knowledge、HPACK、多路复用、流与连接流控、有界头部／body、取消和 GOAWAY；独立对端互通及慢消费者测试。 |
| HTTP/3 | TODO，调研阶段 | 选择 QUIC 传输和 QPACK 实现；TLS 1.3、流上限、拥塞／丢包处理、连接迁移策略和独立对端互通。 |
| Named pipe | TODO | Windows 本地 client/server、消息／字节语义、overlapped IO、访问权限、取消与重启。 |
| Unix domain socket | TODO | 本地 stream client/server、路径所有权与清理、权限、取消与重启；平台特有的 abstract namespace 和 datagram 单独选型。 |
| KCP | TODO，延后开发 C++ 实现 | 先确认线路兼容性，再实现可靠传输、拥塞／窗口控制、定时器、MTU、丢包／乱序测试和性能测量。 |
| RPC | TODO | 与传输解耦的拆包和编解码、请求 ID、截止时间、取消、有界未完成请求和错误传播。 |
| 公开协程 API | TODO | 可等待的连接／读／写、生命周期与取消；用 IO 和 CPU 负载比较共享与分片执行器。 |
| WebSocket 压缩 | TODO | permessage-deflate 协商、解压后上限和恶意帧测试。 |
| DTLS | TODO | 选择可用引擎，定义对端身份、重传和数据报上限。 |

## HTTP 引擎边界

[Boost.Beast](https://github.com/boostorg/beast#introduction) 提供
**HTTP/1 和 WebSocket**，不提供 HTTP/2 或 HTTP/3。修改消息的 HTTP 版本字段
不会增加 HTTP/2 帧协议或 HTTP/3 传输。

nghttp2 是 HTTP/2 协议引擎的候选方案。采用时会增加可选的编译库依赖，
arknet 的 C++ 封装仍可保持 header-only；本版没有引入 nghttp2。
HTTP/3 需要 QUIC/QPACK 协议栈，具体引擎与依赖形式待定。现有 Beast HTTP/1
传输继续独立使用。

## 后续范围

MQTT、数据库协议和 proxy 暂无开发计划，出现具体使用方与验收要求后再评估。
ARK 接入以 arknet 完整验证为前提，不属于本轮重写范围。
