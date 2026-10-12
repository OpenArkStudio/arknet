# UDP Retest After the Fix

Local measurements on 2026-10-12 include 504 matrix runs, 21 receive-buffer isolation runs, and a 60-second UDP stress run for each backend. Independent Release C++ programs produced these results, not CI.

## Test Conditions

| Item | Configuration |
| --- | --- |
| Machine | Mac mini Mac16,11, Apple M4 Pro, 14 cores (10 performance + 4 efficiency), 48 GiB |
| OS | macOS 27.0.1 (26A434), arm64, AC power, low-power mode disabled |
| Toolchain | Apple Clang 21.0.0, CMake 4.3.3, Ninja, C++20, Release `-O3 -DNDEBUG` |
| Dependencies | Standalone Asio 1.38.2 / Boost 1.90.0, OpenSSL 3.6.4; TLS build enabled, sanitizers disabled |
| Network | IPv4 loopback; clients and server in one process; no CPU affinity |
| Measurement | Three independent processes per configuration; 1 s measurement, 0.25 s warmup; isolation runs have no warmup |
| Socket buffers | 64 KiB send / 4 MiB receive; isolation runs vary the server receive capacity |

Matrix source SHA-256: `0771574af8ca56f1914deb544b956079e0a2f24021953ded5f5d524e3df334b1`. Isolation runs add server receive counts and a configurable receive buffer; their archive includes the patch and executable hash.

## Correctness and Load Range

| Group | Standalone | Boost | Result |
| --- | ---: | ---: | --- |
| UDP stress | 60 seconds passed | 60 seconds passed | Data and shutdown checks passed in these runs |
| IO models: 3 payloads × 2 client counts × 5 models | 90 / 90 passed | 90 / 90 passed | Window-1 performance baseline |
| Payload / batch: 3 payloads × 4 windows × 3 models | 93 / 108 passed | 93 / 108 passed | 62 valid configurations and 10 failed configurations combined |
| Size boundaries: 6 payloads × 3 windows | 36 / 54 passed | 36 / 54 passed | Large bursts exceed receive capacity |

All failed records remain archived. A failed repetition excludes all three runs of that configuration from throughput rankings. The source-port collision fix and receive-buffer overflow are separate issues.

## Throughput and Latency

1024-byte datagrams, 16 clients, one outstanding message per client, no extra computation:

![UDP throughput: medians and ranges across IO models](assets/udp-work0-throughput.svg)

![UDP p99 RTT: medians and ranges across IO models](assets/udp-work0-latency.svg)

| Backend | io_contexts / threads | Roundtrips / s | p99 RTT / μs | Process CPU / % |
| --- | --- | ---: | ---: | ---: |
| Standalone | 1 / 1 | 92,515 | 207.79 | 99.4 |
| Standalone | 1 / 2 | 86,611 | 211.58 | 173.3 |
| Boost | 1 / 1 | 90,452 | 207.96 | 99.7 |
| Boost | 1 / 2 | 95,095 | 270.71 | 157.4 |

Standalone has the highest median throughput with one thread at this load. Boost gains some throughput with two threads, at higher p99 latency and CPU use. The older claim that both backends perform best with two threads no longer applies.

Bars show medians; error bars show the minimum and maximum of three runs, not confidence intervals. CPU uses one core as 100% and includes both clients and server. Whole-machine share is process CPU% / 14.

[All IO values](udp-summary.json) · [Complete 64/1024/16384-byte results](udp.md) · [Payload and outstanding-window charts](dimensions-udp.md)

## Receive Buffer Isolation

16 clients, one context/one thread, no warmup. Each configuration runs three times; loss counts below are identical across the three repetitions.

| Payload / B | Outstanding per client | Effective server receive buffer | Total outstanding payload budget | Loss per run |
| --- | ---: | ---: | ---: | ---: |
| 16,384 | 64 | 1 MiB | 16 MiB | 959 |
| 16,384 | 64 | 2 MiB | 16 MiB | 895 |
| 16,384 | 64 | 4 MiB | 16 MiB | 767 |
| 16,384 | 16 | 4 MiB | 4 MiB | 0 |
| 16,384 | 1 | 4 MiB | 256 KiB | 0 |
| 1,472 | 64 | 4 MiB | 1.44 MiB | 0 |
| 1,024 | 1,024 | 4 MiB | 16 MiB | 12,411 |

Every run satisfies: client sends − server receives = missing datagrams; server receives = client echoes. Content and sequence errors are zero. In one 4 MiB / 16 KiB / window-64 run, the kernel's `dropped due to full socket buffers` counter increased from 152,304 to 153,071: exactly 767. IPv4 fragmentation/reassembly increases were zero.

**These losses come from the server receive queue overflowing.** A large burst of small datagrams reproduces the same condition; invalid 16 KiB payloads do not explain it. Loopback still uses finite queues. Total outstanding payload is neither an exact instantaneous queue length nor a universal capacity threshold.

See [Buffers and flow control](../guides/socket-buffers.md) for configuration and the TCP/UDP distinction. Native UDP datagram semantics remain unchanged: no new default throttling, fragmentation or retransmission.

## Reproduce and Inspect Evidence

Compile using the [UDP build instructions](../features/udp/performance.md#build-and-run), then directly run the C++ case:

```sh
build-perf/benchmarks/arknet_loopback_benchmark \
  --protocol udp --payload 16384 --clients 16 --window 64 \
  --io-model shared --io-threads 1 --seconds 1 --warmup 0 \
  --udp-server-receive-buffer 4194304
```

This overflow probe is expected to return a nonzero status. Set the window to 16, or the payload to 1472, to reproduce the corresponding loss-free configurations above.

[Raw records, commands and machine metadata](https://github.com/OpenArkStudio/arknet/tree/main/benchmarks/results/macmini-m4pro-20261012-udp) · [Per-run kernel snapshots](https://github.com/OpenArkStudio/arknet/tree/main/benchmarks/results/macmini-m4pro-20261012-udp/buffer-probes) · [Size-boundary charts](udp-limits.md)

Results cover short IPv4 loopback runs on this machine. Cross-host capacity, IPv6 stress capacity and reliable UDP are not measured. Current coroutine comparisons use native TCP and must not be generalized to UDP.
