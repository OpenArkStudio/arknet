# 配置自动重连

客户端默认启用自动重连，重试延迟为一秒。启动前配置：

```cpp
client.set_auto_reconnect(true, std::chrono::seconds(2));
// 或关闭自动重试：
client.set_auto_reconnect(false);
```

重连成功后，连接的代次会更新。旧队列中的发送不能重放到新连接；过期操作以
`operation_aborted` 完成。在 connect 回调中检查错误后，再恢复应用状态。
