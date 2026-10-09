# Payload, Batch and Execution Comparisons

Local Release IPv4 loopback: 16 clients, three payload sizes, four windows and three thread models, with three repetitions per configuration.

| Configuration | Test machine |
| --- | --- |
| Machine model | Mac16,11 |
| Processor | Apple M4 Pro |
| Architecture | arm64 |
| Physical / logical CPUs | 14 / 14 |
| Memory | 48 GiB |
| Operating system | macOS 27.0.1 (26A434) |
| Kernel release | 27.0.0 |

Measurement 1.0 s, warmup 0.25 s. Charts show medians and min/max, not confidence intervals; p99 is the median of per-run sampled percentiles.

Average process CPU uses 100% for one logical CPU and can exceed 100% with multiple threads; legacy values are marked estimated, and incomplete sets of three CPU samples show N/A. On this 14-logical-CPU host, machine share = process CPU% / 14; 100% of one CPU is 7.14% of the machine.

- Source SHA-256: `fa69d6ce97745281290a263cb0f2515470ede0edb77f5132ce6e3ac7ae7cd9b4`
- Measurements: 2,376; valid profiles: 782; failed profiles: 10.

Apple Clang 21.0.0; CMake 4.3.3; Ninja; Release -O3 -DNDEBUG -std=gnu++20 -arch arm64; TLS ON; sanitizers OFF. Standalone Asio 1.38.2 with BHO Beast based on Boost 1.84 (API 351); Boost 1.90.0 with Asio 1.38.0 and Beast API 359; OpenSSL 3.6.4. Mac mini on AC power, low power mode disabled, no CPU affinity.

The arknet protocol program measures message RTT; matched native Asio callback/coroutine executions measure batch RTT. Their fixed-batch results do not establish speedups over arknet endpoints; public coroutine endpoint APIs remain TODO.

<a id="tcp"></a>

## TCP

[Charts, findings and failed profiles](dimensions-tcp.md)

<a id="udp"></a>

## UDP

[Charts, findings and failed profiles](dimensions-udp.md)

<a id="websocket"></a>

## WebSocket

[Charts, findings and failed profiles](dimensions-websocket.md)

<a id="tcps"></a>

## TCPS

[Charts, findings and failed profiles](dimensions-tcps.md)

<a id="wss"></a>

## WSS

[Charts, findings and failed profiles](dimensions-wss.md)

<a id="http"></a>

## HTTP

[Charts, findings and failed profiles](dimensions-http.md)

<a id="https"></a>

## HTTPS

[Charts, findings and failed profiles](dimensions-https.md)

<a id="native-tcp"></a>

## Native TCP

[Charts, findings and failed profiles](dimensions-native-tcp.md)

## Raw Data

[Complete numeric JSON](dimensions-summary.json ':ignore')

- [standalone-callback.json](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-dimensions/standalone-callback.json): 756 measurements; executable SHA-256 `480122707b4b3ed592d1f580f5cf7092b8008ce52331f55f0e666e89c568be6b`
- [boost-callback.json](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-dimensions/boost-callback.json): 756 measurements; executable SHA-256 `7b2989fd23e15a68a662899d1906f3b094138018b8ada5904309b4a90d96ea34`
- [standalone-native.json](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-dimensions/standalone-native.json): 432 measurements; executable SHA-256 `5d7ac94153f942eda1494656edc865ab01817a7ea9122a240cc5f3260b061549`
- [boost-native.json](https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results/macmini-m4pro-20261010-dimensions/boost-native.json): 432 measurements; executable SHA-256 `d68dc3aea70b8334e5fc88d3bda0739faeb8696842c4f2f582092d92cb1b2d6d`

[Reproduction and metric definitions](../testing.md)

Local loopback, fixed connection distributions and short runs do not establish production capacity; repeat with real messages and connection activity.
