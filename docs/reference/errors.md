# Errors and TLS Verification

## Errors and TLS

Copy thread-local `get_last_error()` in the callback or immediately after the failing
synchronous call; another thread cannot retrieve that error later. For sends, prefer
the explicit completion error or future result.


```cpp
client.bind_connect([]
{
    const arknet::error_code error = arknet::get_last_error();
    if (error) {
        // Record or pass this value to application state.
    }
});
```

TLS clients verify certificate chains and DNS/IP identity by default, with SNI for DNS hosts.
Servers need a usable certificate and matching private key. mTLS also requires a trusted
client CA and server mode `verify_peer | verify_fail_if_no_peer_cert`.
Explicit `verify_none` disables verification.

<Aside type="caution" title="Test credentials">

Repository certificates and keys are for testing only.

</Aside>


For a failed operation, use [Troubleshooting](../project/troubleshooting.md). Current
validation evidence and limits are recorded in [Testing](../project/testing.md).
