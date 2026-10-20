# Performance

These reports measure local loopback workloads on a Mac mini M4 Pro, not CI
runner performance. Choose a report with matching protocol, payload, concurrency
and execution mode; each report records its host, source, commands and scope.

Download the [raw measurements and verification tools](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-benchmark-comparison.tar.gz) with the [SHA256 checksum](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-benchmark-comparison.tar.gz.sha256). The data is distributed as a release asset, outside the source repository.

## Version and execution comparisons

- [Database clients](database-comparison.md): MySQL and Redis throughput, response time and CPU for v0.2.0 and the complete-shutdown implementation.
- [Network endpoints](transport-comparison.md): matched TCP, TLS TCP, HTTP, HTTPS, WebSocket and WSS workloads.
- [Five-second workload comparison](steady-workloads.md): eight selected workloads, five fresh processes per version, with throughput, CPU and response time.
- [Asio callbacks and coroutines](asio-execution-control.md): execution modes and worker counts within the same revision.

The configuration grids contain 1,632 rounds; the five-second comparison contains 80 rounds. Databases use native connection callbacks; the Asio control bypasses arknet endpoints. They do not measure ark's database coroutine bridge.

## RUDP measurements

[RUDP FEC report](rudp.md): 96 configurations and 276 measured runs across both
Asio dependency combinations, three IO models, ARQ-only/fixed/adaptive policies, payloads,
in-flight requests and injected loss. SVG charts compare throughput, p99 RTT,
CPU, wire overhead, recovery, retransmissions and retained memory.
The public RUDP API currently uses callbacks; RUDP coroutines remain TODO.

[RUDP concurrency report](rudp-concurrency.md): 128/512/1,024 clients on one shared io_context, comparing one and four endpoint threads; 18 cases and 54 rounds with throughput, CPU, p99 and whole-process peak RSS.

## Database clients

- [MySQL](../features/mysql/performance.md): read-only text and prepared queries,
  payloads, connections and client queue depth; query RT, throughput, CPU and memory.
- [Redis](../features/redis/performance.md): binary ECHO with pipeline sizes and
  in-flight batches; whole-batch RT, command throughput, CPU and memory.

Both reports compare Boost.Asio and Standalone Asio, plain TCP and verified TLS.
They use callback clients; connection pools and database coroutines are outside these measurements.

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
