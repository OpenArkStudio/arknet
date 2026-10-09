# 架构与扩展

[API 索引](api_CN.md) | [运行时](runtime_CN.md)

## 分层与职责

```text
Application callbacks / HTTP router / custom CRTP types
  TCP       UDP       HTTP/1       WebSocket
   |         |         |               |
   |         |       Beast           Beast
   +---------+---------+---------------+
      client / server / session lifecycle
      bounded sends, owned data, timers, reconnect
                     |
             io_pool / io_t lanes
          owned contexts or external strands
                     |
        standalone Asio or Boost.Asio
            OS sockets; optional OpenSSL
```

CRTP 允许自定义客户端和 Session 扩展协议行为，不需要额外的虚接口。
服务端接受 Session 类型。组件分别负责生命周期、定时器、重连、消息匹配、
事件顺序和发送配额／数据所有权。

standalone 模式使用适配 standalone Asio 的内置 BHO Beast；
Boost 模式使用官方 Boost.Asio 和 Boost.Beast。它们是后端选项，
同一程序中不能混用两种 socket 类型。HTTP/2 与 HTTP/3 需要不同的协议引擎，
见[路线图](roadmap_CN.md)。

## 扩展 Session

```cpp
struct application_session : arknet::tcp_session_t<application_session>
{
    using tcp_session_t::tcp_session_t;
    std::string tenant;
};
arknet::tcp_server_t<application_session> server;
server.bind_recv([](auto& session, std::string_view bytes)
{
    session->async_send(bytes);
});
```

自定义 Session 适合保存连接局部业务状态。跨 Session 的共享配置由应用持有，
多个 lane 并发执行时需要同步。拆包方式与对端协议保持一致。
其他协议族保留相同的继承和组合方式。

## 所有权与执行

接受入队的发送持有数据与配额，直到一次完成回调。
接收 view 和消息引用只在回调期间有效。每个 IO lane 串行执行自己的端点状态，
独立 lane 可以并发。strand 保证执行顺序，不把对象绑定到固定的操作系统线程。

网络对象保持存活至停止完成。回调请求停止，所有者在销毁前等待。
外部 context 持续运行至网络对象停止。外部所有者调用 timer 停止时，
还会等实际取消完成。修改这些关系前应阅读
[运行时约定](runtime_CN.md)和[IO 模型](threading_CN.md)。

## 增加能力

将传输解析和业务分发分开。新端点复用客户端／Session 生命周期与执行器模型，
同时定义自己的线路解析器、内存上限、取消与错误约定。
增加公开头文件、umbrella 导出、后端／TLS 构建覆盖、安装包消费测试，
以及介绍、使用、性能三类指南。

内部组件接口不作为兼容承诺。出现具体协议或使用流程后再增加公共抽象。
[TODO 清单](roadmap_CN.md)记录未来能力，不提供空的占位 API。

## 验证

[测试指南](testing_CN.md)列出功能和持续负载检查。
[本地量化图表](performance/overview_CN.md)记录实际测量的实现和机器。
回环测试通过不证明广域网行为或生产容量。第三方声明见
[第三方声明](THIRD_PARTY_NOTICES_CN.md)。
