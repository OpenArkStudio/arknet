# UDP Load Comparison

16 clients; 64/1024/16384-byte messages; windows 1/4/16/64; three repetitions per configuration.

## No extra application computation

![Round-trip throughput](assets/dimensions-udp-throughput.svg)

![Bidirectional application payload throughput](assets/dimensions-udp-payload.svg)

![Message p99 RTT](assets/dimensions-udp-latency.svg)

- Standalone Asio: At 1024 bytes, the highest throughput median uses 1 context / 1 thread, window 16; the lowest p99 median uses 1 context / 1 thread, window 1.
- Boost.Asio: At 1024 bytes, the highest throughput median uses 1 context / 1 thread, window 1; the lowest p99 median uses 1 context / 1 thread, window 1.

[Complete numeric JSON (p95, run ranges and failures)](dimensions-summary.json ':ignore')

## Failed Profiles

| Backend / execution | Payload bytes / window | Thread model | Application computation | Failure |
| --- | --- | --- | --- | --- |
| Boost.Asio / callback | 16384 / 16 | 4 contexts / one thread each | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1; #2: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1; #3: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1, Warmup errors: 2 |
| Boost.Asio / callback | 16384 / 16 | 1 context / 4 threads | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1; #2: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1; #3: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1, Warmup errors: 2 |
| Boost.Asio / callback | 16384 / 64 | 4 contexts / one thread each | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 769; #2: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 770; #3: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 769 |
| Boost.Asio / callback | 16384 / 64 | 1 context / 1 thread | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 767, Drain timeouts: 1, Warmup errors: 768; #2: benchmark failed with exit code 1; UDP lost messages: 767, Drain timeouts: 1, Warmup errors: 768; #3: benchmark failed with exit code 1; UDP lost messages: 767, Drain timeouts: 1, Warmup errors: 768 |
| Boost.Asio / callback | 16384 / 64 | 1 context / 4 threads | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 769; #2: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 769; #3: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 770 |
| Standalone Asio / callback | 16384 / 16 | 4 contexts / one thread each | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1; #2: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1; #3: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1 |
| Standalone Asio / callback | 16384 / 16 | 1 context / 4 threads | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1; #2: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1, Warmup errors: 2; #3: benchmark failed with exit code 1; UDP lost messages: 1, Drain timeouts: 1, Warmup errors: 2 |
| Standalone Asio / callback | 16384 / 64 | 4 contexts / one thread each | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 769; #2: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 769; #3: benchmark failed with exit code 1; UDP lost messages: 768, Drain timeouts: 1, Warmup errors: 769 |
| Standalone Asio / callback | 16384 / 64 | 1 context / 1 thread | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 767, Drain timeouts: 1, Warmup errors: 768; #2: benchmark failed with exit code 1; UDP lost messages: 767, Drain timeouts: 1, Warmup errors: 768; #3: benchmark failed with exit code 1; UDP lost messages: 767, Drain timeouts: 1, Warmup errors: 768 |
| Standalone Asio / callback | 16384 / 64 | 1 context / 4 threads | No extra application computation | #1: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 769; #2: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 769; #3: benchmark failed with exit code 1; UDP lost messages: 769, Drain timeouts: 1, Warmup errors: 770 |

If any repetition fails, all three measurements for that profile are excluded from rate, latency and matched-ratio summaries; raw failures are retained.

Larger UDP windows can lose traffic; profiles with loss are failed measurements, not capacity results. The server still receives through one serialized socket.

[Metrics, statistics and reproduction](../testing.md)
