# Performance

Mac mini M4 Pro loopback measurements, collected outside CI. Each report records its host, source, commands and scope. Match protocol, payload, concurrency and execution mode when comparing.

Download the [raw measurements and verification tools](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-benchmark-comparison.tar.gz) with the [SHA256 checksum](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.1/arknet-0.2.1-benchmark-comparison.tar.gz.sha256). The data is distributed as a release asset, outside the source repository.

## Version and execution comparisons

<CardGrid>
<LinkCard title="Database clients" href="database-comparison.md" description="MySQL and Redis throughput, response time and CPU: v0.2.0 versus complete shutdown." />
<LinkCard title="Network endpoints" href="transport-comparison.md" description="Matched TCP, TLS TCP, HTTP, HTTPS, WebSocket and WSS workloads." />
<LinkCard title="Five-second workload comparison" href="steady-workloads.md" description="Eight workloads; five fresh processes per version; throughput, CPU and response time." />
<LinkCard title="Asio callbacks and coroutines" href="asio-execution-control.md" description="Execution modes and worker counts within one revision." />
</CardGrid>

The configuration grids contain 1,632 rounds; the five-second comparison contains 80 rounds. Databases use native connection callbacks; the Asio control bypasses arknet endpoints. They do not measure ark's database coroutine bridge.

## RUDP measurements

<CardGrid>
<LinkCard title="RUDP FEC report" href="rudp.md" description="Both Asio dependencies; 96 configurations, 276 runs; three IO models, ARQ-only/fixed/adaptive policies, payloads, in-flight requests and injected loss. Charts cover throughput, p99 RTT, CPU, wire overhead, recovery, retransmissions and retained memory." />
<LinkCard title="RUDP concurrency report" href="rudp-concurrency.md" description="128/512/1,024 clients, one shared io_context, one/four endpoint threads; 18 cases, 54 rounds; throughput, CPU, p99 and process peak RSS." />
</CardGrid>

The public RUDP API uses callbacks; coroutines remain TODO.

## Database clients

<CardGrid>
<LinkCard title="MySQL" href="../features/mysql/performance.md" description="Read-only text/prepared queries; payloads, connections, queue depth; query RT, throughput, CPU and memory." />
<LinkCard title="Redis" href="../features/redis/performance.md" description="Binary ECHO; pipeline sizes, in-flight batches; whole-batch RT, command throughput, CPU and memory." />
</CardGrid>

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

<Aside type="caution" title="Scope">

Measurements do not establish WAN capacity, IPv6 performance or production endurance.

</Aside>

[Methodology and metric definitions](methodology.md) · [Correctness validation](../project/testing.md) · [Buffer configuration](../guides/socket-buffers.md)
