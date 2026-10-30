# UDP Performance Report

Local Release C++ measurements on 2026-10-12: 504 matrix runs, 21 receive-buffer isolation runs and a 60-second UDP stress run per dependency configuration; collected outside CI.

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

Matrix source SHA-256: `0771574af8ca56f1914deb544b956079e0a2f24021953ded5f5d524e3df334b1`. Isolation adds server receive counts and configurable receive buffers; its archive retains the patch and executable hash.

## Correctness and Load Range

| Group | Standalone | Boost | Result |
| --- | ---: | ---: | --- |
| UDP stress | 60 seconds passed | 60 seconds passed | Data and shutdown checks passed in these runs |
| IO models: 3 payloads × 2 client counts × 5 models | 90 / 90 passed | 90 / 90 passed | Window-1 performance baseline |
| Payload / batch: 3 payloads × 4 windows × 3 models | 93 / 108 passed | 93 / 108 passed | 62 valid configurations and 10 failed configurations combined |
| Size boundaries: 6 payloads × 3 windows | 36 / 54 passed | 36 / 54 passed | Large bursts exceed receive capacity |

All failures remain archived; any failed repetition excludes that configuration’s three runs from throughput rankings.

## Throughput and Latency

1024-byte datagrams, 16 clients, one outstanding message per client, no extra computation:

![UDP throughput: medians and ranges across IO models](assets/udp-work0-throughput.svg)

![UDP p99 RTT: medians and ranges across IO models](assets/udp-work0-latency.svg)

| Dependency | io_contexts / threads | Roundtrips / s | p99 RTT / μs | Process CPU / % |
| --- | --- | ---: | ---: | ---: |
| Standalone | 1 / 1 | 92,515 | 207.79 | 99.4 |
| Standalone | 1 / 2 | 86,611 | 211.58 | 173.3 |
| Boost | 1 / 1 | 90,452 | 207.96 | 99.7 |
| Boost | 1 / 2 | 95,095 | 270.71 | 157.4 |

At this load, Standalone’s one thread has the highest throughput median; Boost’s two threads gain throughput at higher p99 and CPU.

Bars: medians; whiskers: three-run min/max, not confidence intervals. CPU includes clients/server, with 100% per core. Machine share = process CPU% / 14.

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

<Aside type="caution" title="Warning: Receive queue overflow">

**Loss comes from server receive queue overflow**, also reproduced with small-datagram bursts. Loopback queues remain finite; outstanding payload is neither instantaneous queue length nor a universal capacity threshold.

</Aside>

[Buffers and flow control](../guides/socket-buffers.md) covers configuration and TCP/UDP differences. Native UDP semantics remain: no new default throttling, fragmentation or retransmission.

## Reproduce and Inspect Evidence

<Steps>

<Step title="Build and run">

Compile using the [UDP build instructions](../features/udp/performance.md#build-and-run), then directly run the C++ case:

```sh
build-perf/benchmarks/arknet_loopback_benchmark \
  --protocol udp --payload 16384 --clients 16 --window 64 \
  --io-model shared --io-threads 1 --seconds 1 --warmup 0 \
  --udp-server-receive-buffer 4194304
```

</Step>

<Step title="Check the expected result">

<Aside type="caution" title="Expected result">

The overflow probe should return nonzero. Window 16 or payload 1472 reproduces the corresponding loss-free configuration above.

</Aside>

</Step>

<Step title="Inspect archived evidence">

[Raw records, commands and machine metadata](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results/macmini-m4pro-20261012-udp) · [Per-run kernel snapshots](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results/macmini-m4pro-20261012-udp/buffer-probes) · [Size-boundary charts](udp-limits.md)

<Aside type="caution" title="Scope">

Short same-host IPv4 loopback only; cross-host and IPv6 stress capacity are unmeasured. [RUDP](rudp.md) is measured separately; native TCP coroutine comparisons do not apply to UDP.

</Aside>

</Step>

</Steps>
