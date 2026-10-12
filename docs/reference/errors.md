# Errors and TLS Verification

## Errors and TLS

`get_last_error()` is thread-local. Read and copy it inside the callback or
immediately after the synchronous call that reports an error. Reading it later
on another thread cannot retrieve the earlier operation's error. A send's
explicit completion error or future result is the preferred error source.

```cpp
client.bind_connect([]
{
    const arknet::error_code error = arknet::get_last_error();
    if (error) {
        // Record or pass this value to application state.
    }
});
```

TLS clients verify certificate chains and DNS/IP identity by default and set
SNI for DNS hosts. Servers need a usable certificate and matching private key.
mTLS also requires a trusted client CA and server verification mode
`verify_peer | verify_fail_if_no_peer_cert`. Explicit `verify_none` disables
verification. Repository certificates and keys are for testing only.

For a failed operation, use [Troubleshooting](../project/troubleshooting.md). Current
validation evidence and limits are recorded in [Testing](../project/testing.md).
