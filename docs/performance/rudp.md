# RUDP Performance

All 96 configurations / 276 measured runs passed: 7,960,439 message round trips; content, order, send-completion and drain checks passed; zero relay errors or queue overflows.

[Concurrency and endpoint threads](rudp-concurrency.md) compares 128/512/1,024 clients with one shared io_context and one or four endpoint threads.

## Test Environment

| Item | Recorded Parameters |
| --- | --- |
| Time (UTC) | 2026-10-13T03:20:31Z / 2026-10-13T04:00:27Z |
| Machine | Mac mini (Mac16,11); Apple M4 Pro; 14 logical CPUs; 48 GiB RAM |
| OS | macOS 27.0.1 (26A434); arm64 |
| Build | Apple clang version 21.0.0 (clang-2100.1.1.101); C++20 Release `-O3 -DNDEBUG -std=gnu++20; no sanitizers`; CMake 4.3.3 |
| Dependencies | Standalone Asio 1.38.2; Boost 1.90.0; OpenSSL 3.6.4 |
| Endpoint Load | 4 clients; complete-message echo; no added business computation; callback API |
| Main Comparison | 54 cases; 3 rounds per case; 2 s warmup; 3 s measured; 1,024 B; 16 in-flight/client |
| Payload / Window | 36 cases; 64 / 4,096 / 16,384 B; windows 1 / 64; random loss; 1 io_context / 1 thread |
| Sustained Runs | 6 cases; one 120 s measured run per case; 4,096 B; window 16; 1 io_context / 4 threads |
| Repeated Seeds | Main and dimension runs use 42 / 43 / 44; separate streams per route and direction |
| Socket Receive Buffers | Effective endpoint values: 4 MiB |
| ARQ | UDP MTU 1200 B; send / receive windows 128 / 512 segments; update interval 10 ms; congestion control on |
| FEC Groups | Up to 8 data shards; fixed parity 2; adaptive parity limit 4; configured group wait 2 ms |
| FEC Budgets | Per-endpoint retention limit 1 MiB; parity datagram bytes limited to 50% of data datagram bytes |
| Relay Capacity | Per route: 8 MiB / 8192 datagrams |

Source SHA-256: `15cb6ced6425d82086a083fd310f6d75016ac3a1018264856d41b460e4412d85`.

## Network Conditions and Metrics

| Condition | Injected Loss | One-Way Delay | Duplication / Reordering |
| --- | --- | ---: | --- |
| No injected loss | 0 | 0 ms | 0 |
| Random loss | 5% per datagram | 10 ms | 1% duplicates; 5% delayed by an extra 15 ms |
| Burst loss | 3 consecutive datagrams per 100, per direction | 10 ms | Same as above |
| Sustained runs | Burst loss plus 2% random loss of remaining datagrams | 10 ms | Same as above |

![RUDP benchmark topology](assets/rudp-fec-topology.svg)

<Aside type="caution" title="Local simulation scope">

Network conditions are simulated in one process on local IPv4 loopback, not WAN measurements.

</Aside>

| Measurement | Scope |
| --- | --- |
| IO model | Charts describe endpoints; relay adds one io_context/thread, sampling another thread |
| Server concurrency | Sessions share a UDP socket/strand; four single-thread contexts mainly distribute clients, not proof of parallel server sharding |
| RTT | Includes simulated delay, congestion control, FEC group wait and ARQ retransmission |
| Throughput | Completed message round trips; elapsed time includes in-flight drain |
| Bars / whiskers | Three-run medians / actual minimum and maximum, not confidence intervals |

## Throughput and p99 RTT

![RUDP FEC throughput](assets/rudp-fec-throughput.svg)

![RUDP FEC p99 RTT](assets/rudp-fec-rtt.svg)

## CPU and Wire Overhead

![RUDP FEC process CPU](assets/rudp-fec-cpu.svg)

Average process CPU includes clients, server, relay and sampling; 100% is one logical CPU, and multiple threads can exceed it. Machine share = process CPU% / 14; the summary retains both measures.

![RUDP FEC wire overhead](assets/rudp-fec-wire-overhead.svg)

Wire overhead = (endpoint UDP payload bytes sent / bidirectional application bytes − 1) × 100%. Includes ARQ control/retransmission, FEC headers, parity and padding; excludes IP/UDP headers and the relay's second hop. This is not the FEC parity ratio.

## Recovery, Retransmission and Retained Memory

![RUDP FEC recovered shards](assets/rudp-fec-recovery.svg)

![RUDP ARQ retransmitted segments](assets/rudp-fec-retransmissions.svg)

Recovery counts FEC-restored data shards; retransmission counts resent ARQ segments. Both use units per 1,000 application round trips and are not complements.

![RUDP FEC sampled retention](assets/rudp-fec-memory.svg)

| Memory metric | Scope and limits |
| --- | --- |
| FEC retention | Endpoint sum sampled every 100 ms; may miss shorter peaks and excludes some temporary codec storage |
| Relay | Sum of independent route peaks, which may occur at different times |
| RSS | Case process high-water mark including warmup and all rounds, not FEC-only memory |

### Resource Values Under Random Loss

1,024 B; window 16; one io_context / one endpoint thread. Sampled peaks are medians across three rounds.

| Dependency / Policy | Restored / 1,000 | Retransmitted / 1,000 | FEC Retained / KiB | Relay Route Peaks Sum / KiB | Process RSS / MiB | Max Observed Parity |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Standalone Asio / ARQ only | 0.0 | 331.7 | 0.0 | 50.5 | 6.8 | 0 |
| Standalone Asio / Fixed FEC | 182.8 | 185.9 | 37.7 | 84.3 | 7.1 | 2 |
| Standalone Asio / Adaptive FEC | 176.7 | 181.8 | 30.8 | 83.4 | 7.2 | 3 |
| Boost.Asio / ARQ only | 0.0 | 327.3 | 0.0 | 52.4 | 7.0 | 0 |
| Boost.Asio / Fixed FEC | 182.2 | 166.5 | 37.2 | 87.1 | 7.3 | 2 |
| Boost.Asio / Adaptive FEC | 186.2 | 178.0 | 33.7 | 93.8 | 7.3 | 3 |

## Payload Size and In-Flight Requests

Random loss; one io_context / one endpoint thread. Cells: round trips/s / p99 RTT in ms, three-run medians. Full JSON adds CPU, wire overhead and resource counters.

### Standalone Asio

| Message / B | In-Flight per Client | ARQ Only | Fixed FEC | Adaptive FEC |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 1 | 139 / 80.8 | 116 / 91.4 | 140 / 79.6 |
| 64 | 64 | 450 / 876.3 | 421 / 941.3 | 419 / 920.6 |
| 4,096 | 1 | 74 / 139.4 | 114 / 93.5 | 113 / 88.5 |
| 4,096 | 64 | 146 / 2,096.2 | 223 / 1,432.5 | 234 / 1,285.4 |
| 16,384 | 1 | 20 / 344.9 | 29 / 240.2 | 31 / 228.5 |
| 16,384 | 64 | 42 / 6,239.0 | 64 / 4,019.4 | 67 / 4,034.0 |

### Boost.Asio

| Message / B | In-Flight per Client | ARQ Only | Fixed FEC | Adaptive FEC |
| ---: | ---: | ---: | ---: | ---: |
| 64 | 1 | 138 / 81.9 | 117 / 96.8 | 142 / 78.4 |
| 64 | 64 | 435 / 853.7 | 446 / 895.7 | 406 / 998.8 |
| 4,096 | 1 | 75 / 141.5 | 119 / 93.5 | 115 / 95.5 |
| 4,096 | 64 | 142 / 2,108.6 | 228 / 1,342.6 | 232 / 1,247.0 |
| 16,384 | 1 | 21 / 383.5 | 30 / 235.1 | 31 / 264.8 |
| 16,384 | 64 | 41 / 6,212.4 | 63 / 4,157.4 | 66 / 4,102.8 |

Messages fragment to the protocol MTU: 16 KiB is an application message, not a UDP datagram. FEC reserves 26 B of the 1,200 B UDP payload ceiling, leaving 1,174 B per ARQ datagram.

## 120-Second Sustained Runs

One measured run per case, without repeated-run whiskers; all message checks passed. Memory follows the definitions above; peaks cannot rule out long-term leaks.

| Dependency / Policy | Message Round Trips | Round Trips/s | p99 / ms | CPU / % | Wire Overhead / % | FEC Sampled Peak / KiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Standalone Asio / ARQ only | 20,734 | 172 | 599.6 | 48.6 | 24.2 | 0.0 |
| Standalone Asio / Fixed FEC | 27,622 | 229 | 448.3 | 82.1 | 66.2 | 232.6 |
| Standalone Asio / Adaptive FEC | 29,058 | 241 | 453.6 | 84.9 | 70.3 | 166.8 |
| Boost.Asio / ARQ only | 20,870 | 173 | 606.4 | 50.3 | 23.9 | 0.0 |
| Boost.Asio / Fixed FEC | 27,666 | 230 | 453.8 | 80.2 | 66.4 | 242.6 |
| Boost.Asio / Adaptive FEC | 29,136 | 242 | 445.3 | 84.7 | 70.0 | 148.5 |

## Results and Policy Selection

Main matrix, one io_context / one endpoint thread; rank policies within the same IO model.

| Dependency | Network Condition | Highest Round-Trip Throughput | Lowest p99 RTT |
| --- | --- | ---: | ---: |
| Standalone Asio | No injected loss | ARQ only 62,827 round trips/s | ARQ only 1.1 ms |
| Standalone Asio | Random loss | Fixed FEC 844 round trips/s | Adaptive FEC 142.5 ms |
| Standalone Asio | Burst loss | Adaptive FEC 914 round trips/s | Adaptive FEC 139.5 ms |
| Boost.Asio | No injected loss | ARQ only 61,705 round trips/s | ARQ only 1.1 ms |
| Boost.Asio | Random loss | Fixed FEC 869 round trips/s | Fixed FEC 160.0 ms |
| Boost.Asio | Burst loss | Adaptive FEC 878 round trips/s | Adaptive FEC 150.7 ms |

The ranges below span the two dependency configurations' three-run medians:

- No injected loss: ARQ only reached 61,705 - 62,827 round trips/s, with p99 1.06 - 1.09 ms. Both throughput and p99 favored ARQ-only over the measured FEC policies on both dependency configurations.
- Random loss: FEC throughput was 835 - 869 round trips/s versus 560 - 572 for ARQ only. Process CPU increased from 25.4 - 26.6% to 52.7 - 56.2%, and wire-byte overhead from 21.7 - 21.9% to 55.1 - 60.1%.
- Random loss, 64 B and one in-flight request per client: fixed FEC reached 116 - 117 round trips/s, below ARQ-only's 138 - 139. Small-message, low-concurrency workloads need separate evaluation.

These results do not establish a universal winner; budget p99, CPU, wire bytes and memory together. Fixed FEC parity depends on group density and byte budgets; adaptive FEC needs sufficient samples and adjustment time. Full values retain realized loss, observed parity and policy changes.

<Aside type="caution" title="Measurement and protocol limits">

No cross-host, real-WAN, IPv6 stress or RUDP coroutine measurements; the public API uses callbacks, with coroutines TODO. FEC provides no authentication or encryption; see [compatibility and security](../features/rudp/fec.md#compatibility-and-security).

</Aside>

ARQ-only and both FEC policies share the relay topology; compare under matching network conditions and endpoint IO models.

## Reproduction and Full Data

<Steps>

<Step title="Build and run the C++ case">

```sh
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build-perf --target arknet_rudp_benchmark
build-perf/benchmarks/arknet_rudp_benchmark --payload 1024 --clients 4 --window 16 \
  --io-model shared --io-threads 1 --fec adaptive --profile random \
  --loss-percent 5 --delay-ms 10 --duplicate-percent 1 --reorder-percent 5 \
  --seconds 3 --warmup 2 --rounds 3 --seed 42
```

Configure Boost in a separate build directory with `-DARKNET_USE_BOOST_ASIO=ON`. Cases run as C++ executables; maintainers batch with `bash benchmarks/run_rudp_fec.sh BUILD_DIR OUTPUT_DIR`, without Python or Ruby. Raw data retains each actual command, exit code and stderr file.

</Step>

<Step title="Generate the offline report">

The optional offline report tool needs only Node; it does not launch network tests:

```sh
node benchmarks/report_rudp_fec.mjs --self-check
node benchmarks/report_rudp_fec.mjs \
  --results /path/to/arknet-results/rudp-fec --output docs/performance
```

</Step>

</Steps>

[Full Metrics and Measurement Metadata](rudp-fec-summary.json)

[Raw JSON, Actual Commands and Exit Codes](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results/macmini-m4pro-20261013/rudp-fec)
