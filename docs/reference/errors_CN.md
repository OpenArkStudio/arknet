# 错误处理与 TLS 校验

## 错误和 TLS

在回调内或同步调用报告错误后，立即复制线程局部的 `get_last_error()`；其他线程无法取回该错误。
发送优先使用显式完成错误或 future 结果。


```cpp
client.bind_connect([]
{
    const arknet::error_code error = arknet::get_last_error();
    if (error) {
        // 记录错误，或将错误值传入应用状态。
    }
});
```

TLS 客户端默认验证证书链与 DNS/IP 身份，DNS 主机发送 SNI。服务端须提供可用证书与匹配的私钥。
mTLS 还需信任客户端 CA，并在服务端设置 `verify_peer | verify_fail_if_no_peer_cert`。
显式 `verify_none` 关闭验证。

<Aside type="caution" title="测试凭据">

仓库中的证书与私钥仅用于测试。

</Aside>


操作失败时，按[故障排查](../project/troubleshooting_CN.md)检查。当前验证证据和范围限制见
[测试](../project/testing_CN.md)。
