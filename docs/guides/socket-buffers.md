# Buffers and Flow Control

TCP and UDP both use finite socket buffers. Buffer capacity, incoming bursts and receive processing speed determine queue growth, including on loopback.

## Three Different Buffers

| Layer | Configuration | When capacity runs out |
| --- | --- | --- |
| arknet application send queue | `set_max_send_buffer_size(bytes)`; defaults to 16 MiB and at most 1024 operations | `async_send()` rejects the submission with `no_buffer_space` |
| Kernel socket send / receive buffers | `SO_SNDBUF` / `SO_RCVBUF` | TCP queues and applies backpressure; UDP drops datagrams when the receive queue is full |
| Application receive and protocol parsing buffer | Constructor initial / maximum buffer sizes | Limits parsing or an individual receive; does not enlarge the kernel receive queue |

Raising the application send limit does not enlarge `SO_RCVBUF`. Adding IO threads does not make the serial receive callbacks on one UDP socket run in parallel.

## TCP and UDP Behavior

| Transport | When the receiver falls behind | Successful send means |
| --- | --- | --- |
| TCP, also underlying TLS, HTTP and WebSocket | The advertised receive window shrinks or closes, slowing the sender; retransmission recovers network loss. Applications may see higher latency or timeouts | Data passed to the local transport, not processed by the peer application |
| Native UDP | A full queue drops further datagrams, with no automatic feedback, retransmission or ordering guarantee | The local stack accepted the datagram, not that the peer received it |

TCP ACKs acknowledge transport reception; the receive window advertises remaining capacity. Neither confirms business processing. TCP connections can still fail through timeouts, disconnections or resource exhaustion.

<Aside type="caution" title="Warning: Loopback can lose UDP datagrams">

A full UDP receive queue drops datagrams even on loopback. A successful send does not confirm reception. Buffer capacity must account for bursts and receiver processing time.

</Aside>

arknet does not automatically fragment, retransmit or throttle native UDP. Use the experimental [RUDP module](../features/rudp/index.md) for reliable, ordered messages over UDP.

## Set and Read Back the Effective Size

Configure clients in `bind_init`, after the socket opens and before connecting. Reconnection runs this callback again. Configure each accepted TCP server connection separately. All UDP server Sessions share one socket: configure it once in the server's `bind_init` callback.

| Object | Set | Read |
| --- | --- | --- |
| TCP / UDP clients, TCP Sessions and their TLS / HTTP / WebSocket derivatives | `set_sndbuf_size(bytes)`, `set_rcvbuf_size(bytes)` | `get_sndbuf_size()`, `get_rcvbuf_size()` |
| UDP server | `acceptor().set_option(asio::socket_base::send_buffer_size(bytes))` or `receive_buffer_size(bytes)` | `acceptor().get_option(option)` |

<Aside type="tip" title="Tip: Read back the effective buffer size">

The **4 MiB below is an example, not a universal recommendation**. Check errors immediately after each call and record the effective size: the OS may cap or reject a request. Queries overwrite the thread-local `get_last_error()`.

</Aside>

### TCP or UDP Client

```cpp
#include <iostream>

#include <arknet/arknet.hpp>

arknet::udp_client client; // Use arknet::tcp_client for TCP.
client.bind_init([&] {
    client.set_rcvbuf_size(4 * 1024 * 1024);
    if (const auto error = arknet::get_last_error()) {
        std::cerr << "SO_RCVBUF: " << error.message() << '\n';
        client.request_stop();
        return;
    }
    const int actual = client.get_rcvbuf_size();
    if (const auto error = arknet::get_last_error()) {
        std::cerr << "getsockopt: " << error.message() << '\n';
        client.request_stop();
        return;
    }
    std::cout << "SO_RCVBUF=" << actual << " bytes\n";
});
```

### TCP Server

```cpp
arknet::tcp_server server;
server.bind_accept([](auto& session) {
    if (arknet::get_last_error()) return;
    session->set_rcvbuf_size(4 * 1024 * 1024);
    if (const auto error = arknet::get_last_error()) {
        std::cerr << "SO_RCVBUF: " << error.message() << '\n';
        session->stop();
        return;
    }
    const int actual = session->get_rcvbuf_size();
    if (const auto error = arknet::get_last_error()) {
        std::cerr << "getsockopt: " << error.message() << '\n';
        session->stop();
        return;
    }
    std::cout << "SO_RCVBUF=" << actual << " bytes\n";
});
```

This callback runs after TCP acceptance. It cannot change TCP window scaling negotiated in the completed handshake. For settings that must precede the handshake, verify listening socket configuration and inheritance on the target OS.

### UDP Server

```cpp
arknet::udp_server server;
server.bind_init([&] {
    arknet::error_code error;
    server.acceptor().set_option(
        asio::socket_base::receive_buffer_size(4 * 1024 * 1024), error);
    if (error) {
        std::cerr << "SO_RCVBUF: " << error.message() << '\n';
        server.request_stop();
        return;
    }
    asio::socket_base::receive_buffer_size actual;
    server.acceptor().get_option(actual, error);
    if (error) {
        std::cerr << "getsockopt: " << error.message() << '\n';
        server.request_stop();
        return;
    }
    std::cout << "SO_RCVBUF=" << actual.value() << " bytes\n";
});
```

These snippets configure sockets. See [TCP usage](../features/tcp/index.md) and [UDP usage](../features/udp/index.md) for complete startup, messaging and shutdown examples. Configure send buffers the same way using `set_sndbuf_size` or `send_buffer_size`.

## Size Buffers for the Workload

- For clients bursting into one UDP server, estimate total burst payload: client count × outstanding messages per client × datagram bytes. Kernel bookkeeping consumes capacity too; `SO_RCVBUF` does not translate to an exact packet count.
- For a receive pause of T seconds at an input rate of R bytes/s, evaluate at least R × T bytes of backlog and measure headroom using real bursts and handler duration. The queue must drain after the burst.
- For high-bandwidth, long-distance TCP connections, consider bandwidth × RTT worth of outstanding data. Measure OS autotuning first; explicit settings can constrain it on some systems.
- TCP connections have separate socket budgets; UDP server Sessions share a receive queue. Larger buffers increase memory budgets and potential queueing latency, not application processing speed.

Sustained input above processing capacity eventually exhausts any finite buffer. Shorten receive callbacks, adjust business concurrency or apply suitable flow control. Use TCP or a reliable UDP protocol when delivery must be reliable.

## OS Limits and Diagnostics

| Platform | Inspect |
| --- | --- |
| Linux | `sysctl net.core.rmem_max net.core.wmem_max`; TCP autotuning also uses `net.ipv4.tcp_rmem` / `tcp_wmem`. Inspect UDP receive errors and buffer overflow with `nstat` or `netstat -su` |
| macOS | `sysctl kern.ipc.maxsockbuf net.inet.udp.recvspace net.inet.udp.sendspace`; inspect `dropped due to full socket buffers` in `netstat -s -p udp` |
| Windows | Read socket options and use process/network performance counters and packet capture. POSIX sysctl values do not apply |

Requests can be capped or rejected: always record effective values. Linux generally reports a doubled `SO_RCVBUF` / `SO_SNDBUF` budget that includes kernel overhead, so equal queried values on Linux and macOS do not imply equal payload capacity. Kernel counters can be system-wide; compare snapshots around an isolated load and correlate application send/receive counts.

The [2026-10-12 UDP diagnosis](../performance/udp-update.md#receive-buffer-isolation) observed a 16 MiB burst into a 4 MiB receive budget: 767 datagrams were missing and the kernel overflow counter increased by 767. Smaller bursts or different buffer sizes changed loss; a large burst of 1 KiB datagrams reproduced it too. MTU and maximum datagram length are separate constraints; see [UDP limits](../performance/udp-limits.md).

[Lifecycle and application backpressure](../concepts/sending.md) · [Testing and metrics](../performance/methodology.md)
