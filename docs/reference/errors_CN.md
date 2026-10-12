# 错误处理与 TLS 校验

## 错误和 TLS

`get_last_error()` 使用线程局部存储。在回调内或报告错误的同步调用之后立即读取
并复制错误值；稍后在另一个线程读取，无法得到原操作的错误。发送优先使用显式的
完成错误或 future 结果。

```cpp
client.bind_connect([]
{
    const arknet::error_code error = arknet::get_last_error();
    if (error) {
        // 记录错误，或将错误值传入应用状态。
    }
});
```

TLS 客户端默认验证证书链和 DNS/IP 身份，并为 DNS 主机设置 SNI。服务端需要可用
证书及匹配的私钥。mTLS 还需信任客户端 CA，并在服务端设置
`verify_peer | verify_fail_if_no_peer_cert`。显式设置 `verify_none` 会关闭验证。
仓库中的证书和私钥仅用于测试。

操作失败时，按[故障排查](../project/troubleshooting_CN.md)检查。当前验证证据和范围限制见
[测试](../project/testing_CN.md)。
