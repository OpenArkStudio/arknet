# Performance

These reports measure local loopback workloads on a Mac mini M4 Pro, not CI
runner performance. Choose a report with matching protocol, payload, concurrency
and execution mode; each report records its host, source, commands and scope.

## RUDP measurements

[RUDP FEC report](rudp.md): 96 configurations and 276 measured runs across both
Asio dependency combinations, three IO models, ARQ-only/fixed/adaptive policies, payloads,
in-flight requests and injected loss. SVG charts compare throughput, p99 RTT,
CPU, wire overhead, recovery, retransmissions and retained memory.
The public RUDP API currently uses callbacks; RUDP coroutines remain TODO.

## UDP measurements

[UDP performance report](udp-update.md) includes 504 matrix runs, 21 buffer probes and
a 60-second stress test for each dependency combination.

- [IO models](udp.md): one context/one thread, one context/multiple threads and partitioned contexts.
- [Payload and in-flight requests](dimensions-udp.md): repeated configurations with failure groups retained.
- [Limits and loss](udp-limits.md): datagram sizes and receive-queue overflow.

## Callback, coroutine and CPU comparisons

[Matched native TCP measurements](dimensions-native-tcp.md) compare callbacks and
coroutines under the same batch workload. [IO model comparisons](io-comparison.md)
explain the original scheduler results and their measurement boundaries.
These programs use native Asio; public arknet coroutine endpoints remain TODO.

## Protocol results

| Protocol | IO model results | Payload and batch results |
| --- | --- | --- |
| TCP | [Results](tcp.md) | [Results](dimensions-tcp.md) |
| TLS TCP | [Results](tcps.md) | [Results](dimensions-tcps.md) |
| WebSocket | [Results](websocket.md) | [Results](dimensions-websocket.md) |
| WSS | [Results](wss.md) | [Results](dimensions-wss.md) |
| HTTP | [Results](http.md) | [Results](dimensions-http.md) |
| HTTPS | [Results](https.md) | [Results](dimensions-https.md) |

The [multi-protocol summary](overview.md) and [dimension summary](dimensions.md)
show the final results for each measured workload, with dates and source hashes.
Measurements do not establish WAN capacity, IPv6 performance or production endurance.

[Methodology and metric definitions](methodology.md) · [Correctness validation](../project/testing.md) · [Buffer configuration](../guides/socket-buffers.md)
