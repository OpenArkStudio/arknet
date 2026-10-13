# RUDP configuration

Configure both endpoints while stopped. Invalid settings throw
`std::invalid_argument`; changing a running endpoint throws `std::logic_error`.
See [FEC policies](fec.md) for optional coding and statistics.

```cpp
arknet::arq::options settings;
settings.mtu = 1200;
settings.send_window = 128;
settings.receive_window = 512;
settings.max_pending_bytes = 4 * 1024 * 1024;
server.set_rudp_options(settings);
client.set_rudp_options(settings);
```

## Defaults and limits

| Setting | Default | Meaning |
| --- | ---: | --- |
| `mtu` | 1200 bytes | Maximum outbound UDP payload, including ARQ headers |
| `send_window` | 128 segments | Upper bound on unacknowledged outbound segments |
| `receive_window` | 512 segments | Receive ordering and completed-message window |
| `interval` | 10 ms | ARQ timer tick |
| `nodelay` | `true` | Lower minimum retransmission timeout |
| `congestion_control` | `true` | Reduce/increase the send window according to loss and ACKs |
| `fast_resend` | 2 | Later ACKs required to trigger fast retransmission; zero disables it |
| `max_retransmits` | 20 | Retries after the initial transmission |
| `max_pending_bytes` | 4 MiB | Per-direction ARQ payload plus segment-header budget; also bounds queued raw sends |

ACK-driven fast retransmission stops after six total transmissions; timeout
retransmission remains available. This keeps a dense or duplicate ACK burst from
exhausting the retry budget before the peer can respond.

The server defaults to **1024 active sessions**, configured with
`set_max_sessions(count)` before start. At capacity, it skips new admission;
clients can retry, and existing sessions still receive valid handshake replies.
Application-retained stopped sessions are outside the active-session count and
remain the holder's memory responsibility.

Each segment has a 24-byte header. A message uses at most 256 fragments; with
the default MTU, the maximum payload is **301,056 bytes**. Both peers' receive
windows must hold all fragments of the largest message. Lowering MTU increases
fragment count. Configure MTU for the actual path: 1200 is a conservative
default, not a guarantee that every path avoids IP fragmentation.

## Sending and overload

`async_send` copies data and reports a completion once. Its return value reports
admission into arknet's outer send queue; the callback/future may later report
`no_buffer_space` when the ARQ queue is full. `send` returns accepted payload
bytes, so zero-length success must be distinguished using the error code.

Oversized messages report `message_size`. Incoming valid segments that exceed
the ARQ receive budget close the session with `no_buffer_space`; malformed
segments are ignored. Retry exhaustion reports `timed_out`. OS socket buffers
are separate from ARQ queues: follow [socket buffer configuration](../../guides/socket-buffers.md).

## Execution and shutdown

The built-in IO pool, one context with several threads, and several contexts
with one thread each use the same endpoint API. Protocol state and callbacks
run through the endpoint's strand. External contexts must keep running until
`stop()` completes. Do not block an IO callback waiting for a future.

A server attempts FIN notices before closing its shared socket. FIN is
best effort and can be lost. A client discovers a silent failed peer when
outstanding data exhausts retries. ARQ-only has no automatic idle heartbeat.
FEC mode sends feedback each sample window even without application messages,
maintaining peer session liveness while the connection is idle.
Server sessions also use their configurable silence timeout. Reconnect
establishes a new conversation, without replaying the old queue.
A SYN with a different sync identifier does not replace an active session.
Reusing the same source endpoint requires closing the old session with a valid
FIN or waiting for its silence timeout (60 seconds by default).
Silence elapsed time uses a monotonic clock. Invalid data, wrong conversations and
stale handshakes do not extend session liveness.

## Wire compatibility

The ARQ core implements KCP message-mode PUSH/ACK/WASK/WINS segments and
little-endian 24-byte headers. Tests exchange messages in both directions
with the independent canonical C implementation, including loss, duplication,
reordering and zero-window recovery. Stream mode is not supported.

The RUDP transport adds a 5-byte SYN (`type=1`, `sync32`) and 9-byte ACK/FIN
(`type=2/3`, `sync32`, `conversation32`) envelope compatible with ARK's existing
RDP setup. Core interoperability and envelope layout are tested separately;
an end-to-end ARK migration has not yet been validated. A bare KCP peer needs
to implement this envelope or use `arknet::arq::control_block` directly.

The core is standard-library-only and header-only. Calls must be serialized;
the output callback must not reenter it, and output views expire when the
callback returns. `send` returns 0 on admission, -1 for an unsupported size or
failed state, and -2 for the send quota. `input` returns 0 on success, -5 for
the receive quota, and other negative values for invalid datagrams.

Neither the envelope nor the ARQ core authenticates packets. Use trusted
networks or an application security design appropriate to the deployment.
