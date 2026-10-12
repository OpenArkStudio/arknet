# TLS TCP Configuration

The [client](client.md) and [server](server.md) examples provide complete startup, messaging and shutdown programs. Apply configuration before starting the relevant endpoint unless stated otherwise.

## Require a Client Certificate

For mTLS, add the following before the example's server start call:

```cpp
arknet::error_code server_error;
server.load_verify_file(certs + "/ca.pem", server_error);
if (server_error)
    return 1;
server.set_verify_mode(asio::ssl::verify_peer |
    asio::ssl::verify_fail_if_no_peer_cert, server_error);
if (server_error)
    return 1;
```

After constructing the client and before its start call, supply its identity:

```cpp
client.set_cert_file(certs + "/ca.pem", certs + "/client.pem",
    certs + "/client-key.pem", "");
if (arknet::get_last_error())
    return 1;
```

Keep client server-identity verification enabled. Without a trusted client
certificate, the server rejects the connection. A TLS 1.3 client may briefly
report start success before receiving that rejection; handle disconnect events
and application readiness instead of relying on start alone for mTLS admission.
Map verified certificate identity to application permissions separately.

## Apply the Same Policy to WSS and HTTPS

For WSS, use `wss_client`/`wss_server` from
`<arknet/http/wss_client.hpp>` and `<arknet/http/wss_server.hpp>`. Configure
identity and CA trust in the same way. Start the server without `use_dgram` and
start the client with `(host, port, "/echo")`. WebSocket supplies message
boundaries and performs its HTTP upgrade after TLS; see
[WebSocket usage](../websocket/index.md) for frame types and upgrade headers.

For HTTPS, use `https_client`/`https_server` and the corresponding headers.
Certificate and mTLS policy remain the same, while send/receive data are HTTP
request/response objects rather than byte views. See [HTTP usage](../http/index.md).
HTTPS does not add WebSocket upgrade routing or change application authorization.

All secure endpoint families expose `bind_handshake` and `ssl_stream()`.
For handshake diagnosis, register a client callback before start:

```cpp
client.bind_handshake([&]
{
    const auto error = arknet::get_last_error();
    if (error)
        std::cerr << "TLS handshake: " << error.message() << '\n';
});
```

An untrusted CA and a DNS/IP identity mismatch are different failures. Check
the host passed to `start`, the certificate's subject alternative names, trust
configuration and validity period. Connecting by numeric IP requires an IP
identity in the certificate; setting a DNS SNI name is not a substitute.

## Shutdown and Extension

Secure sends retain normal payload ownership, completion signatures and bounded
queues. TLS write completion is not remote application acknowledgment. Configure
`set_connect_timeout` and `set_disconnect_timeout` before start; shutdown attempts
TLS close-notify within the disconnect bound. WSS closes its WebSocket protocol
first, then TLS and TCP.

Use `tcps_client_t`, `tcps_session_t` and `tcps_server_t` for CRTP extensions;
WSS and HTTPS have their matching `_t` types. Native SSL context options are
available through inheritance. Configure them before starting, and serialize
stream access with the endpoint's IO lane. Reconnect creates a fresh TLS stream
and performs verification again; do not assume an old native stream reference
remains valid.

Request stop from an IO callback and wait on the owner thread. Any external
context must keep running until stop completes; destroy endpoints before
stopping the host runners. See [IO models](../../concepts/io-models.md),
[security regressions](https://github.com/OpenArkStudio/arknet/blob/main/tests/tls.cpp)
and [performance](performance.md).
