# TLS

TLS adds transport encryption and certificate-based peer verification to TCP,
WebSocket and HTTP. Enable `ARKNET_ENABLE_SSL=ON` and use the exported
`arknet::arknet` interface target to include the optional OpenSSL dependency.
The supported minimum protocol version is TLS 1.2.

## Secure Endpoints

| Plaintext | Secure variant | Application protocol |
| --- | --- | --- |
| `tcp_client`, `tcp_server`, `tcp_session` | `tcps_client`, `tcps_server`, `tcps_session` | Byte streams and TCP receive policies |
| `ws_client`, `ws_server`, `ws_session` | `wss_client`, `wss_server`, `wss_session` | WebSocket messages after TLS and HTTP upgrade |
| `http_client`, `http_server`, `http_session` | `https_client`, `https_server`, `https_session` | HTTP messages over TLS |

Secure endpoints retain the CRTP extension and component-composition model,
including custom sessions, send ownership, queue bounds, timers and external IO.
The names use `ssl` in APIs such as `ssl_stream()` because that is Asio's type
name; the configured protocol is modern TLS. This release has no UDP DTLS.

## Verification Defaults

Clients verify the certificate chain and the DNS name or IP address passed to
`start`. System trust paths are loaded by default; a private CA can be added with
`load_verify_file`. DNS connections send SNI, while numeric IP connections do
not. A trusted certificate with the wrong DNS/IP identity still fails.

Servers need a certificate chain and matching private key before starting.
`set_cert_file` loads PEM files, and `set_cert_buffer` loads PEM contents.
`bind_handshake` reports TLS handshake completion before the usable connection
event; WSS then performs its HTTP upgrade. TLS failures are reported through
the normal error and disconnect lifecycle.

Explicit `verify_none` disables client verification. Use this only for a
deliberate local experiment; it removes the peer-identity protection that the
defaults provide. The repository's certificates and private keys are public
test resources and must not be deployed.

## Mutual TLS

mTLS requires the client to prove possession of a trusted certificate as well.
Configure the server with a trusted client CA and
`asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert`, and configure
the client certificate/key. Both sides still need their normal trust policy.

This is suitable for controlled service connections and managed devices.
Certificate trust establishes a cryptographic peer identity; application
authorization still maps that identity to permitted actions. A TLS 1.3 client
may finish its handshake before receiving a server's missing-client-certificate
alert, so connection/disconnect events remain part of failure handling.

## Lifetime and Configuration

Configure identity, trust and verification before starting endpoints. Native
Asio SSL context APIs remain available for options beyond the convenience
loaders. Coordinate later configuration changes with endpoint lifetime; there
is no automatic certificate-rotation service.

`ssl_stream()` exposes the native TLS stream. Stream operations follow the
endpoint's serialized IO lane; do not overlap native writes with arknet's send
queue. Shutdown bounds TLS close-notify waiting with the disconnect timeout.
Keep external contexts running until stop completes.

## Next Steps

- [Usage](usage.md): verified TCP echo, mTLS, WSS and HTTPS configuration.
- [Performance](performance.md): steady-state TCPS/WSS/HTTPS measurement limits.
- [TLS tests](https://github.com/OpenArkStudio/arknet/blob/main/tests/tls.cpp): trust, DNS/IP rejection, SNI, mTLS and reconnect.
- [HTTP](../http.md) and [WebSocket](../websocket/overview.md): application-level message APIs.
