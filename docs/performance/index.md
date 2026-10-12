# Performance

These reports measure local loopback workloads on a Mac mini M4 Pro, not CI
runner performance. Choose a report with matching protocol, payload, concurrency
and execution mode; each report records its host, source, commands and scope.

## RUDP measurements

[2026-10-12 RUDP report](rudp.md): 216 runs across both Asio backends,
64/1024/4096/16384-byte messages, 1/4/16 in-flight requests and three IO models.
Charts compare round trips per second, p99 RTT and process CPU utilization.
The public RUDP API currently uses callbacks; coroutine and FEC comparisons
are not included.

## Current UDP measurements

[2026-10-12 retest](udp-update.md) includes 504 matrix runs, 21 buffer probes and
a 60-second stress test for each backend after the UDP source-port fix.

- [IO models](udp.md): one context/one thread, one context/multiple threads and partitioned contexts.
- [Payload and in-flight requests](dimensions-udp.md): repeated configurations with failure groups retained.
- [Limits and loss](udp-limits.md): datagram sizes and receive-queue overflow.

## Callback, coroutine and CPU comparisons

[Matched native TCP measurements](dimensions-native-tcp.md) compare callbacks and
coroutines under the same batch workload. [IO model comparisons](io-comparison.md)
explain the original scheduler results and their measurement boundaries.
These programs use native Asio; public arknet coroutine endpoints remain TODO.

## Protocol baselines

| Protocol | IO model results | Payload and batch results |
| --- | --- | --- |
| TCP | [Results](tcp.md) | [Results](dimensions-tcp.md) |
| TLS TCP | [Results](tcps.md) | [Results](dimensions-tcps.md) |
| WebSocket | [Results](websocket.md) | [Results](dimensions-websocket.md) |
| WSS | [Results](wss.md) | [Results](dimensions-wss.md) |
| HTTP | [Results](http.md) | [Results](dimensions-http.md) |
| HTTPS | [Results](https.md) | [Results](dimensions-https.md) |

The [2026-10-10 multi-protocol summary](overview.md) and [dimension summary](dimensions.md)
are historical baselines. Their UDP results predate the source-port fix; use the
2026-10-12 report for current UDP conclusions. Measurements do not establish WAN
capacity, IPv6 performance or production endurance.

[Methodology and metric definitions](methodology.md) · [Correctness validation](../project/testing.md) · [Buffer configuration](../guides/socket-buffers.md)
