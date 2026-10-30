# 配置自动重连

客户端默认启用自动重连，重试延迟为一秒。启动前配置：

```cpp
client.set_auto_reconnect(true, std::chrono::seconds(2));
// 或关闭自动重试：
client.set_auto_reconnect(false);
```

<Aside type="caution" title="重连不重放发送">

重连成功后更新连接代次；旧队列操作以 `operation_aborted` 完成，不向新连接重放。
在 connect 回调中检查错误后，再恢复应用状态。

</Aside>
