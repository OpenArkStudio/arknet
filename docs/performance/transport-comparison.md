# Network endpoint performance comparison

Compare v0.2.0 with the implementation that waits for native operations and callback capture retirement. Each of 64 configurations runs three fresh processes per version: 384 measured rounds, with no failed requests, content errors or drain timeouts.

The [five-second workload comparison](steady-workloads.md) includes independent five-run WebSocket, TCP and HTTP measurements.

CPU combines the colocated client and server; one full core is 100%. RT is per-message roundtrip time. Both versions use the same driver.

| Environment | Recorded value |
| --- | --- |
| Machine | Mac mini (Mac16,11) · arm64 |
| CPU | Apple M4 Pro · 14 physical cores / 14 logical cores (system CPU count 14) |
| Memory | 48 GiB |
| OS | macOS 27.0.1 · build 26A434 |
| Compiler | Apple clang version 21.0.0 (clang-2100.3.34.2) · gnu++20 |
| Build flags | Release · -O3 -DNDEBUG |
| Dependencies | Boost 1.90 · Standalone Asio 1.38.2 · OpenSSL 3.6.4 25 Aug 2026 (Library: OpenSSL 3.6.4 25 Aug 2026) |
| Build tools | CMake 4.3.3 · Ninja 1.13.2 |
| Servers | Colocated local endpoints built from each recorded library commit |
| Measurement interval (UTC) | 2026-10-15T19:07:59Z → 2026-10-15T19:18:36Z |
| Warmup / measurement | 0.25 s / 1 s × 3 fresh processes |
| API / IO | Public arknet endpoint callbacks · One io_context with 1 / 4 workers |
| CPU sampling | Colocated client + server, measured phase plus drain; one full core = 100% |

v0.2.0 library: `25fe2e5fac4e09edee597f312d58b0362b9e0681` · Candidate: `2aa5bc0ef7d383d8cfbc04520f3b952fa27c5fbd`

`loopback.cpp` SHA256: `1749828c0388136bb7cea4559303c0f59b4d5d3833280e9e52ecb0bee41d17c8`

## TCP · Boost.Asio

16 connections · 64 B / 4096 B · 1 / 16 in flight · one io_context with 1 / 4 threads

![TCP · Boost.Asio · Throughput · roundtrips/s](assets/comparison-020-2aa5bc0-transport-boost-tcp-throughput.svg)

![TCP · Boost.Asio · CPU occupation · %](assets/comparison-020-2aa5bc0-transport-boost-tcp-cpu.svg)

<details>
<summary>P50 and P99 response time</summary>

![TCP · Boost.Asio · P50 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-tcp-p50.svg)

![TCP · Boost.Asio · P99 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-tcp-p99.svg)

</details>

Shown 8 cases: median per-case throughput ratio change -2.34%.Each summary is a median of case ratios, not pooled throughput.

## TCP over TLS · Boost.Asio

16 connections · 64 B / 4096 B · 1 / 16 in flight · one io_context with 1 / 4 threads

<details>
<summary>Throughput, CPU and response time</summary>

![TCP over TLS · Boost.Asio · Throughput · roundtrips/s](assets/comparison-020-2aa5bc0-transport-boost-tcps-throughput.svg)

![TCP over TLS · Boost.Asio · P50 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-tcps-p50.svg)

![TCP over TLS · Boost.Asio · P99 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-tcps-p99.svg)

![TCP over TLS · Boost.Asio · CPU occupation · %](assets/comparison-020-2aa5bc0-transport-boost-tcps-cpu.svg)

</details>

Shown 8 cases: median per-case throughput ratio change -0.8%.Each summary is a median of case ratios, not pooled throughput.

## HTTP/1.1 · Boost.Asio

16 connections · 64 B / 4096 B · 1 / 16 in flight · one io_context with 1 / 4 threads

<details>
<summary>Throughput, CPU and response time</summary>

![HTTP/1.1 · Boost.Asio · Throughput · roundtrips/s](assets/comparison-020-2aa5bc0-transport-boost-http-throughput.svg)

![HTTP/1.1 · Boost.Asio · P50 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-http-p50.svg)

![HTTP/1.1 · Boost.Asio · P99 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-http-p99.svg)

![HTTP/1.1 · Boost.Asio · CPU occupation · %](assets/comparison-020-2aa5bc0-transport-boost-http-cpu.svg)

</details>

Shown 8 cases: median per-case throughput ratio change -1.44%.Each summary is a median of case ratios, not pooled throughput.

## HTTPS · Boost.Asio

16 connections · 64 B / 4096 B · 1 / 16 in flight · one io_context with 1 / 4 threads

<details>
<summary>Throughput, CPU and response time</summary>

![HTTPS · Boost.Asio · Throughput · roundtrips/s](assets/comparison-020-2aa5bc0-transport-boost-https-throughput.svg)

![HTTPS · Boost.Asio · P50 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-https-p50.svg)

![HTTPS · Boost.Asio · P99 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-https-p99.svg)

![HTTPS · Boost.Asio · CPU occupation · %](assets/comparison-020-2aa5bc0-transport-boost-https-cpu.svg)

</details>

Shown 8 cases: median per-case throughput ratio change -1.82%.Each summary is a median of case ratios, not pooled throughput.

## WebSocket · Boost.Asio

16 connections · 64 B / 4096 B · 1 / 16 in flight · one io_context with 1 / 4 threads

<details>
<summary>Throughput, CPU and response time</summary>

![WebSocket · Boost.Asio · Throughput · roundtrips/s](assets/comparison-020-2aa5bc0-transport-boost-websocket-throughput.svg)

![WebSocket · Boost.Asio · P50 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-websocket-p50.svg)

![WebSocket · Boost.Asio · P99 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-websocket-p99.svg)

![WebSocket · Boost.Asio · CPU occupation · %](assets/comparison-020-2aa5bc0-transport-boost-websocket-cpu.svg)

</details>

Shown 8 cases: median per-case throughput ratio change -3.02%.Each summary is a median of case ratios, not pooled throughput.

## WebSocket over TLS · Boost.Asio

16 connections · 64 B / 4096 B · 1 / 16 in flight · one io_context with 1 / 4 threads

<details>
<summary>Throughput, CPU and response time</summary>

![WebSocket over TLS · Boost.Asio · Throughput · roundtrips/s](assets/comparison-020-2aa5bc0-transport-boost-wss-throughput.svg)

![WebSocket over TLS · Boost.Asio · P50 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-wss-p50.svg)

![WebSocket over TLS · Boost.Asio · P99 response time · µs](assets/comparison-020-2aa5bc0-transport-boost-wss-p99.svg)

![WebSocket over TLS · Boost.Asio · CPU occupation · %](assets/comparison-020-2aa5bc0-transport-boost-wss-cpu.svg)

</details>

Shown 8 cases: median per-case throughput ratio change 0.15%.Each summary is a median of case ratios, not pooled throughput.

## TCP · Standalone Asio

16 connections · 64 B / 4096 B · 1 / 16 in flight · one io_context with 1 / 4 threads

<details>
<summary>Throughput, CPU and response time</summary>

![TCP · Standalone Asio · Throughput · roundtrips/s](assets/comparison-020-2aa5bc0-transport-standalone-tcp-throughput.svg)

![TCP · Standalone Asio · P50 response time · µs](assets/comparison-020-2aa5bc0-transport-standalone-tcp-p50.svg)

![TCP · Standalone Asio · P99 response time · µs](assets/comparison-020-2aa5bc0-transport-standalone-tcp-p99.svg)

![TCP · Standalone Asio · CPU occupation · %](assets/comparison-020-2aa5bc0-transport-standalone-tcp-cpu.svg)

</details>

Shown 8 cases: median per-case throughput ratio change -0.5%.Each summary is a median of case ratios, not pooled throughput.

## TCP over TLS · Standalone Asio

16 connections · 64 B / 4096 B · 1 / 16 in flight · one io_context with 1 / 4 threads

<details>
<summary>Throughput, CPU and response time</summary>

![TCP over TLS · Standalone Asio · Throughput · roundtrips/s](assets/comparison-020-2aa5bc0-transport-standalone-tcps-throughput.svg)

![TCP over TLS · Standalone Asio · P50 response time · µs](assets/comparison-020-2aa5bc0-transport-standalone-tcps-p50.svg)

![TCP over TLS · Standalone Asio · P99 response time · µs](assets/comparison-020-2aa5bc0-transport-standalone-tcps-p99.svg)

![TCP over TLS · Standalone Asio · CPU occupation · %](assets/comparison-020-2aa5bc0-transport-standalone-tcps-cpu.svg)

</details>

Shown 8 cases: median per-case throughput ratio change -0.31%.Each summary is a median of case ratios, not pooled throughput.

<details>
<summary>All data (median [min–max])</summary>

| Case | Throughput/s: reference library → candidate library | P50 µs: reference library → candidate library | P99 µs: reference library → candidate library | CPU%: reference library → candidate library | RSS MiB: reference library → candidate library |
| --- | ---: | ---: | ---: | ---: | ---: |
| HTTP/1.1 · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 1 | 111,038.06 [110,277.54–111,425.45] → 109,593.61 [106,131.93–110,672.49] | 142.29 [141.79–143.29] → 143.88 [143.75–144.13] | 151.38 [150.71–153.04] → 157 [153.13–272.08] | 99 [99–99.01] → 98.99 [97.68–99.98] | 10.33 [10.3–10.34] → 10.45 [10.45–10.52] |
| HTTP/1.1 · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 4 | 172,991.57 [172,433.35–178,101.66] → 173,247.32 [168,544.06–173,429.12] | 90.25 [88.63–91] → 90.25 [90.25–91.13] | 114.96 [113.58–137.25] → 129.83 [123.5–158.25] | 373.58 [373.02–380.38] → 376.96 [374.59–377.85] | 10.47 [10.44–10.56] → 10.66 [10.58–10.7] |
| HTTP/1.1 · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 1 | 150,967.01 [147,666.92–151,043.91] → 148,575.84 [147,050.84–150,345.31] | 1,691.67 [1,673.63–1,704.71] → 1,718.71 [1,697.33–1,720.96] | 1,847.88 [1,811.08–2,249.83] → 1,852.04 [1,814–1,930.5] | 99.02 [98.98–99.92] → 99.86 [99.02–99.92] | 10.45 [10.38–10.52] → 10.61 [10.55–10.61] |
| HTTP/1.1 · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 4 | 224,341.11 [211,869.32–232,089.43] → 210,165.68 [206,564.62–225,010.04] | 1,139.38 [1,098.83–1,191.88] → 1,203.38 [1,123.5–1,221.29] | 1,275 [1,248.54–1,356.42] → 1,340.96 [1,252.71–1,451.92] | 372.66 [360.4–374.17] → 361.9 [361.85–371.23] | 10.58 [10.58–10.63] → 10.73 [10.66–10.83] |
| HTTP/1.1 · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 1 | 44,872.03 [44,209.17–45,035.98] → 43,768.74 [43,465.88–45,017.15] | 352.42 [342.67–354.08] → 362.13 [342.71–364.08] | 386.71 [377.67–390.67] → 387.5 [380.71–391.54] | 81.4 [79.17–82.6] → 79.52 [79.01–84] | 10.7 [10.63–10.72] → 10.75 [10.63–10.75] |
| HTTP/1.1 · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 4 | 101,296.44 [99,313.58–101,556.54] → 100,317.96 [100,046.97–103,183.71] | 152.75 [151.96–152.96] → 153.63 [151.17–154.04] | 225.58 [215.54–244.33] → 196.54 [183.79–198.92] | 376.15 [375.45–380.2] → 376.66 [376.33–382.3] | 10.83 [10.81–10.89] → 10.78 [10.75–10.91] |
| HTTP/1.1 · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 1 | 55,961.68 [53,749.6–56,294.13] → 52,650.05 [52,600.07–52,962.24] | 4,541.08 [4,533.25–4,787.79] → 4,811.75 [4,794.33–4,825.75] | 4,685.46 [4,627.25–5,082.04] → 5,095.29 [4,925.96–5,113.29] | 87.37 [83.19–88.07] → 82.02 [81.92–82.27] | 13.34 [13.33–13.41] → 13.5 [13.5–13.52] |
| HTTP/1.1 · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 4 | 110,034.99 [108,249.82–113,957.13] → 112,350.97 [108,494.65–112,751.48] | 2,311.04 [2,240.17–2,330.54] → 2,256.92 [2,243.71–2,263.04] | 2,472.54 [2,395.54–2,577.83] → 2,690.33 [2,403.04–4,041.13] | 374.8 [370.69–381.08] → 377.4 [374.33–379.79] | 13.81 [13.42–13.81] → 13.97 [13.8–14.08] |
| HTTPS · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 1 | 79,997.77 [78,879.41–80,778.48] → 78,676.79 [78,660.6–80,258.77] | 198.42 [197.92–200.54] → 201.04 [198.79–201.63] | 209.83 [207.92–212.46] → 210.92 [209.33–212.96] | 99.34 [99.01–99.98] → 99.01 [99–99.89] | 34.53 [34.48–34.66] → 34.7 [34.67–34.84] |
| HTTPS · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 4 | 156,786.67 [154,461.22–158,499.92] → 153,183.8 [149,921.71–158,382.56] | 99.79 [98.79–100.08] → 102.33 [98.63–104.08] | 137.63 [134.29–159.21] → 141.54 [133.38–155.71] | 381.92 [380.13–384.17] → 381.01 [378.29–384.82] | 34.95 [34.94–34.97] → 35.25 [35.17–35.27] |
| HTTPS · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 1 | 105,952.78 [104,408.96–107,407.24] → 104,208.87 [104,008.98–106,494.73] | 2,391.38 [2,379.88–2,426.46] → 2,425 [2,393.58–2,437.17] | 2,491.88 [2,468.5–2,530.13] → 2,571.46 [2,543–2,580.13] | 99.09 [99.09–99.96] → 99.1 [99.08–99.94] | 34.75 [34.64–34.97] → 34.98 [34.89–35.05] |
| HTTPS · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 4 | 195,776.15 [188,040.56–199,063.3] → 183,395.73 [182,056–195,717.63] | 1,288.42 [1,278.88–1,343.79] → 1,376.58 [1,287.29–1,382.38] | 1,445.88 [1,419.29–1,506.63] → 1,595.17 [1,448–1,600.04] | 378.66 [373.24–383.68] → 372.4 [372.05–378.03] | 35.08 [35.06–35.16] → 35.42 [35.41–35.53] |
| HTTPS · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 1 | 35,591.19 [34,848.8–36,146.07] → 34,243.03 [34,087.56–34,376.23] | 435.71 [422.71–452.46] → 462.42 [458.33–462.75] | 484.63 [477.71–486.04] → 486.13 [485.96–497.17] | 85.15 [82.84–88.68] → 82.48 [82.37–82.62] | 35.53 [35.47–35.58] → 35.58 [35.47–35.58] |
| HTTPS · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 4 | 95,870.36 [94,039.66–97,443.98] → 94,085.18 [93,269.38–95,042.34] | 162.04 [159.79–163.13] → 164.67 [164.04–167.17] | 214.79 [208.71–249.33] → 216.96 [203.29–231.92] | 383.01 [379.86–387.96] → 384.08 [381.7–384.45] | 35.61 [35.61–35.69] → 35.7 [35.64–35.88] |
| HTTPS · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 1 | 42,688.44 [42,292.18–42,844.78] → 41,928.67 [41,848.82–42,140] | 5,944.67 [5,925.21–6,005.5] → 6,075.29 [5,983.75–6,090.92] | 6,119 [6,115.67–6,215] → 6,276.54 [6,194.29–6,289.25] | 90.58 [87.87–90.62] → 88.37 [87.86–89.6] | 38.67 [38.52–38.67] → 38.64 [38.53–38.8] |
| HTTPS · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 4 | 104,994.53 [103,257.09–105,806.63] → 103,405.63 [101,422.35–104,814.45] | 2,412.96 [2,409.42–2,413.58] → 2,440.71 [2,432.04–2,514.25] | 2,548.71 [2,546.38–4,095.83] → 2,860.54 [2,782.63–2,909.79] | 382.37 [381.04–385.77] → 381.63 [380.89–386.58] | 38.98 [38.88–39.11] → 39.14 [39.03–39.16] |
| TCP · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 1 | 162,064.08 [158,479.52–166,024.81] → 160,521.37 [159,696.45–160,585.25] | 96.67 [96.04–97.21] → 98.25 [98.17–99.21] | 116.83 [114.67–162.71] → 118.83 [118.71–138.54] | 98.99 [98.79–99.98] → 98.99 [98.97–99.98] | 9.34 [9.31–9.39] → 9.5 [9.48–9.53] |
| TCP · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 4 | 193,155.81 [192,487.93–195,296.34] → 193,117.76 [192,929.02–193,809.9] | 78.17 [77.67–78.75] → 78.13 [77.88–78.42] | 116.17 [115.75–119.17] → 113.71 [110–118.79] | 267.46 [267.23–269.87] → 269.09 [268.16–269.47] | 9.45 [9.41–9.47] → 9.52 [9.5–9.58] |
| TCP · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 1 | 212,323.87 [207,216.58–216,765.34] → 204,393.35 [203,430.74–214,672.46] | 1,196.71 [1,172.71–1,266.79] → 1,231.83 [1,198.79–1,269.04] | 1,251.5 [1,232.92–1,299.33] → 1,316.33 [1,237.46–1,323] | 94.58 [90.08–95.1] → 93.75 [93.5–94.36] | 9.48 [9.48–9.52] → 9.64 [9.56–9.64] |
| TCP · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 4 | 310,292.84 [276,296.84–316,216.35] → 284,003.56 [270,564.75–304,817.44] | 820 [808.38–940.54] → 910.79 [822.71–958.83] | 1,099 [1,085.63–1,112.96] → 1,126.42 [1,116.21–1,178.88] | 356.35 [345.87–358.26] → 351.18 [345.43–357.77] | 9.53 [9.52–9.53] → 9.67 [9.63–9.8] |
| TCP · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 1 | 101,478.01 [101,367.46–102,047.04] → 100,845.46 [100,812.06–101,055.57] | 152.58 [152.54–153.13] → 153.71 [153.46–153.92] | 174.08 [173.46–174.88] → 175.54 [174.67–175.54] | 99.01 [99–99.54] → 99.01 [99.01–99.01] | 10.02 [10–10.05] → 10.06 [10.05–10.11] |
| TCP · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 4 | 162,346.45 [161,384.08–163,223.54] → 162,801.45 [161,587.4–163,146.44] | 95.67 [94.71–95.96] → 95.21 [95.04–95.38] | 129.46 [129.29–130.83] → 136.92 [130.54–138.71] | 277.5 [276.46–277.59] → 278.65 [277.77–280] | 10.03 [9.98–10.08] → 10.08 [10.06–10.08] |
| TCP · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 1 | 128,683.83 [126,777.4–129,794.69] → 121,560.42 [120,300.09–123,435.96] | 1,958.25 [1,920–2,007.21] → 2,090.88 [2,084.75–2,100.33] | 2,105.67 [2,100.5–2,115.79] → 2,144.63 [2,140.08–2,714.13] | 98.63 [95.73–99.04] → 91.54 [91.23–93.3] | 12.5 [12.48–12.5] → 12.72 [12.66–12.73] |
| TCP · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 4 | 256,750.95 [247,096.42–264,258.53] → 242,461.24 [237,208.36–254,299.41] | 979.04 [940.38–1,024.92] → 1,047.67 [987.38–1,068.46] | 1,316.92 [1,282.5–1,361.04] → 1,395.54 [1,287.75–1,446.46] | 372.76 [369.43–374.95] → 370.17 [368.56–373.54] | 12.59 [12.55–12.61] → 12.94 [12.91–12.94] |
| TCP over TLS · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 1 | 106,395.15 [106,022.01–107,256.55] → 103,973.13 [103,924.76–104,210.55] | 147.5 [146.46–149.17] → 150.88 [150.46–151.04] | 169.63 [167.58–170.33] → 173.17 [173.08–173.33] | 99.01 [99–99.69] → 99 [99–99.01] | 33.77 [33.67–33.84] → 33.89 [33.83–34.09] |
| TCP over TLS · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 4 | 159,727.96 [159,028.63–160,443.49] → 159,207.75 [158,332.65–159,272.22] | 97.67 [97.63–98] → 98.71 [98.29–99.04] | 133.08 [131.92–134.5] → 133.67 [131.13–149.58] | 288.49 [288.4–288.71] → 291.56 [289.66–293.28] | 34.09 [34.08–34.22] → 34.27 [34.22–34.3] |
| TCP over TLS · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 1 | 137,604.98 [130,689.84–140,149.27] → 130,843.62 [127,939.82–138,751.5] | 1,835.08 [1,819.96–1,991.5] → 1,939.58 [1,829.58–1,986.96] | 1,952.21 [1,942.92–2,078.29] → 1,987.67 [1,893.17–2,089.04] | 96.51 [94.59–99.17] → 97.46 [94.79–99.17] | 33.91 [33.89–33.91] → 34.16 [34–34.27] |
| TCP over TLS · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 4 | 259,300.3 [236,215.69–264,678.19] → 243,068.36 [232,155.25–259,341.03] | 982.08 [960.71–1,079.29] → 1,050.63 [981.21–1,094.92] | 1,207.54 [1,203.63–1,254.71] → 1,243.33 [1,222.46–1,311.75] | 375.33 [365.54–376.04] → 370.16 [363.78–375.2] | 34.27 [34.22–34.28] → 34.5 [34.45–34.53] |
| TCP over TLS · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 1 | 60,987.07 [60,305.37–61,166.85] → 60,397.28 [60,367.37–60,685.6] | 255.29 [254.29–258.08] → 257.75 [256.54–257.79] | 281.04 [279.33–282.83] → 283.08 [282.67–288.42] | 99.01 [99.01–99.01] → 99.01 [98.99–99.02] | 34.73 [34.72–34.75] → 35 [34.92–35.09] |
| TCP over TLS · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 4 | 121,729.78 [121,244.57–122,209.2] → 121,852.86 [120,944.09–122,631.5] | 129 [128.83–129.38] → 129.04 [128.21–130.29] | 160.04 [157.83–160.92] → 163.21 [159.46–177.71] | 307.38 [307.11–309.34] → 310.6 [307.28–311.41] | 34.98 [34.94–35.03] → 35.3 [34.98–35.33] |
| TCP over TLS · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 1 | 68,150.06 [68,126.89–68,604.43] → 68,130.96 [67,188.54–68,982.76] | 3,720.21 [3,700.71–3,755.54] → 3,719.79 [3,690.29–3,769.63] | 3,866.13 [3,783.58–3,877.08] → 4,023.38 [3,805.29–4,146.17] | 95.42 [94.72–97.03] → 97 [96.51–97.4] | 37.77 [37.63–37.86] → 38.03 [38–38.14] |
| TCP over TLS · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 4 | 187,738.2 [181,211.82–189,746.23] → 186,549.68 [178,707.54–188,344.36] | 1,341.54 [1,337.25–1,389.04] → 1,353.88 [1,337.21–1,416.83] | 1,511.25 [1,474.21–1,584.08] → 1,549.38 [1,544.38–1,620.21] | 387.9 [383.49–389.28] → 387.37 [383.38–388.22] | 37.95 [37.91–37.97] → 38.36 [38.14–38.44] |
| WebSocket · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 1 | 134,193.62 [133,920.03–135,207.59] → 129,869.64 [129,063.54–130,441.12] | 117.5 [116.96–117.63] → 121.5 [121.08–122.21] | 135.96 [135.5–136.25] → 139.5 [138.08–140.63] | 99.01 [99–99.27] → 99 [99–99] | 10.72 [10.7–10.78] → 10.84 [10.83–10.91] |
| WebSocket · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 4 | 168,896.97 [138,443.97–170,895.11] → 168,615.6 [168,097.69–169,536.18] | 91.38 [90.42–104] → 92 [91.5–92.17] | 139.54 [119.42–321.38] → 120.83 [120.21–137.54] | 302.78 [298.6–303.79] → 308.02 [307.18–309.03] | 10.91 [10.84–10.94] → 11.02 [11.02–11.06] |
| WebSocket · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 1 | 163,888.89 [162,377.69–172,107.17] → 155,467 [152,918.84–164,052.07] | 1,565.5 [1,306.63–1,572.58] → 1,627.46 [1,468.75–1,657.29] | 1,653.04 [1,630.21–2,563.71] → 2,140.75 [1,906.17–3,081.88] | 91.57 [90.58–94.6] → 96.27 [92.88–98.42] | 10.98 [10.95–11.03] → 11.02 [11.02–11.11] |
| WebSocket · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 4 | 219,694.22 [219,559.15–233,093.74] → 206,437.03 [206,397.39–215,817.87] | 1,165.33 [1,095.08–1,166.83] → 1,237.08 [1,188.38–1,240.25] | 1,354.63 [1,279.42–1,358.42] → 1,436.21 [1,370.38–1,470.54] | 357 [356.8–363.54] → 353.22 [352.55–358.42] | 11.17 [11.17–11.17] → 11.22 [11.19–11.22] |
| WebSocket · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 1 | 74,730.32 [73,870–75,234.4] → 73,570.67 [73,117.08–74,363.17] | 211.04 [209.42–213.5] → 213.5 [211.5–213.67] | 222.5 [219.29–224.5] → 224.92 [223–225.58] | 99.99 [99.93–99.99] → 99.44 [99–99.98] | 11.14 [11.09–11.14] → 11.23 [11.2–11.31] |
| WebSocket · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 4 | 141,132.86 [140,903.11–147,996.92] → 142,299.22 [138,747.36–144,861.95] | 107.42 [102.83–107.75] → 105.79 [104.83–109.54] | 152.17 [145.25–158.88] → 159.42 [151.33–185.38] | 371.02 [370.98–378.44] → 376.56 [371.41–378.37] | 11.22 [11.2–11.28] → 11.41 [11.31–11.44] |
| WebSocket · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 1 | 83,992.24 [83,475.66–86,097.13] → 81,622.76 [81,589.45–86,269.79] | 3,034.79 [2,899.67–3,040.13] → 3,109.88 [2,913.96–3,110.13] | 3,095.33 [3,094.42–3,102.42] → 3,162.33 [3,123.63–3,191.58] | 94.55 [94.11–96.97] → 94.32 [94.17–98.31] | 13.8 [13.78–13.86] → 13.88 [13.86–13.94] |
| WebSocket · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 4 | 146,668.43 [146,088.97–149,570.12] → 137,961.22 [137,397.6–141,878.55] | 1,713.42 [1,681.17–1,720.38] → 1,772.13 [1,772.04–1,821.42] | 2,079.08 [2,039.25–2,112.25] → 2,213.71 [2,182.13–3,148.79] | 359.75 [358.85–363.39] → 358.37 [357.67–358.79] | 14.02 [13.86–14.06] → 13.88 [13.81–14] |
| WebSocket over TLS · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 1 | 87,540.12 [86,936.58–87,700.39] → 86,802.64 [86,040.8–88,361.35] | 180.79 [179.88–180.92] → 181.21 [179.67–182.88] | 204.42 [203.21–205.04] → 204.79 [203.67–207.33] | 99.16 [99.03–99.61] → 99.01 [99–99.79] | 35.45 [35.41–35.48] → 35.72 [35.47–35.77] |
| WebSocket over TLS · Boost.Asio · 16 connections · 64 B · in flight 1 · threads 4 | 143,881.69 [142,399.96–144,786.87] → 144,197.09 [143,650.12–145,047.89] | 108.63 [108–109.75] → 108.25 [107.63–109.13] | 137.83 [135.67–139.58] → 137.29 [136.29–140.75] | 321.74 [321.5–322.24] → 322.76 [322.54–323.2] | 35.84 [35.64–35.91] → 35.95 [35.92–35.97] |
| WebSocket over TLS · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 1 | 103,531.06 [100,851.85–107,556.89] → 102,024.6 [99,548.75–102,686.03] | 2,468.54 [2,346–2,521.71] → 2,486 [2,476.96–2,552.88] | 2,586.29 [2,536.63–2,611.58] → 2,571.54 [2,560–2,754.88] | 96.53 [95.02–98.82] → 95.98 [95.78–96.4] | 35.64 [35.64–35.78] → 35.63 [35.59–35.72] |
| WebSocket over TLS · Boost.Asio · 16 connections · 64 B · in flight 16 · threads 4 | 176,603.86 [175,594.93–182,241.9] → 176,738.48 [172,681.68–188,997.78] | 1,433.71 [1,391.38–1,445.79] → 1,435.46 [1,318.38–1,466.38] | 1,575.83 [1,512–1,588.21] → 1,673.71 [1,576.29–2,079.33] | 361.75 [361.43–365.9] → 362.69 [361.63–371.63] | 35.92 [35.73–35.95] → 36.14 [35.95–36.19] |
| WebSocket over TLS · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 1 | 49,736.55 [49,681.51–49,757.44] → 49,871.74 [49,308.42–49,872.9] | 316 [315.25–316] → 317.17 [315.46–319] | 330.25 [328.58–333.96] → 330.83 [328.54–331.67] | 99.01 [99.01–99.02] → 99.02 [99.01–99.7] | 36.19 [36.09–36.34] → 36.3 [36.03–36.36] |
| WebSocket over TLS · Boost.Asio · 16 connections · 4096 B · in flight 1 · threads 4 | 118,631.58 [118,192.29–121,138.63] → 120,601.43 [116,960.13–121,633.22] | 128.96 [126.17–129.33] → 126.46 [125.58–129.42] | 173.71 [171.79–180.08] → 183.38 [171.96–219.83] | 379.35 [379.22–383.72] → 383.25 [380.72–383.88] | 36.39 [36.2–36.53] → 36.48 [36.33–36.84] |
| WebSocket over TLS · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 1 | 56,093.47 [55,295.11–56,637.96] → 55,256.9 [54,794.26–55,333.91] | 4,542.67 [4,496.13–4,592.04] → 4,631.13 [4,594.54–4,633.63] | 4,719.71 [4,716.13–4,740.83] → 4,751.96 [4,745.21–4,757.29] | 99.05 [98.44–99.25] → 98.53 [98.39–99.1] | 38.95 [38.88–39] → 39.06 [39.03–39.25] |
| WebSocket over TLS · Boost.Asio · 16 connections · 4096 B · in flight 16 · threads 4 | 129,126.84 [126,786.94–131,869.86] → 131,095.82 [126,723.93–131,615.74] | 1,910 [1,909.88–1,985.79] → 1,924.25 [1,911.79–2,003.33] | 2,392.71 [2,313.92–2,909.88] → 2,319.83 [2,301.75–2,429.79] | 379.37 [375.94–382.24] → 381.32 [379.14–381.82] | 39.23 [39.16–39.31] → 39.39 [39.33–39.41] |
| TCP · Standalone Asio · 16 connections · 64 B · in flight 1 · threads 1 | 163,356.78 [162,821.23–163,456.3] → 160,653.61 [160,220.98–160,657.44] | 96.5 [96.25–96.54] → 98.08 [97.79–98.67] | 115.67 [114.83–142.5] → 117.08 [116.96–117.75] | 99 [99–99.73] → 99 [99–99.93] | 6.53 [6.47–6.58] → 6.55 [6.47–6.55] |
| TCP · Standalone Asio · 16 connections · 64 B · in flight 1 · threads 4 | 195,121.64 [192,866.75–195,833.64] → 192,488.84 [191,307.02–193,015] | 77.79 [77.25–78.25] → 78.88 [78.83–78.96] | 114.92 [110.63–115.29] → 112.25 [109.17–122.54] | 268.41 [266.99–270.69] → 272.4 [271.66–272.95] | 6.53 [6.52–6.55] → 6.61 [6.56–6.63] |
| TCP · Standalone Asio · 16 connections · 64 B · in flight 16 · threads 1 | 212,491.91 [210,908.91–221,288.13] → 216,495.58 [213,237.91–221,168.5] | 1,169.46 [1,166.08–1,259.79] → 1,183.29 [1,135.79–1,189.92] | 1,278.42 [1,195.13–1,293.63] → 1,307.88 [1,282.96–1,308.5] | 93.31 [90.07–93.5] → 97.82 [96.98–99.75] | 6.61 [6.55–6.64] → 6.66 [6.64–6.72] |
| TCP · Standalone Asio · 16 connections · 64 B · in flight 16 · threads 4 | 293,240.41 [288,552.53–313,261.15] → 290,122.24 [268,407.12–315,819.58] | 868.42 [809.83–880.42] → 884.33 [801.29–970.29] | 1,150.5 [1,137.79–1,171.71] → 1,152.46 [1,118.21–1,161.75] | 351.19 [350.51–358.78] → 353.03 [347.22–361.27] | 6.72 [6.69–6.75] → 6.75 [6.72–6.77] |
| TCP · Standalone Asio · 16 connections · 4096 B · in flight 1 · threads 1 | 101,658.76 [100,454.67–102,091.84] → 102,131.61 [101,288.75–102,281.67] | 151.33 [150.42–151.83] → 152.67 [152.54–152.75] | 206.79 [183.63–215.63] → 174.33 [174.21–174.54] | 98.92 [98.91–98.94] → 99.78 [99–99.8] | 6.98 [6.98–7] → 7.23 [7.2–7.27] |
| TCP · Standalone Asio · 16 connections · 4096 B · in flight 1 · threads 4 | 161,607.47 [160,889.25–163,455.52] → 161,719.12 [161,063.73–162,143.11] | 95.83 [95.38–95.88] → 95.83 [95.75–95.88] | 133.96 [133.29–134.17] → 128.13 [125.83–136.17] | 276.85 [276.71–279.41] → 278.33 [278.2–278.63] | 7.13 [7.11–7.16] → 7.25 [7.2–7.3] |
| TCP · Standalone Asio · 16 connections · 4096 B · in flight 16 · threads 1 | 125,464.71 [122,678.68–127,225.61] → 124,076.34 [123,335.8–133,556.67] | 2,056.79 [1,963.42–2,061.21] → 2,079.88 [1,895–2,083.54] | 2,109.5 [2,106.75–2,117.63] → 2,141.46 [1,976.5–2,155.5] | 92.99 [90.85–95.62] → 93.26 [92.36–99.12] | 9.66 [9.64–9.7] → 9.64 [9.63–9.64] |
| TCP · Standalone Asio · 16 connections · 4096 B · in flight 16 · threads 4 | 245,818.63 [225,149.62–270,400.45] → 246,809.5 [222,210.48–253,337.08] | 1,021.04 [917.46–1,036.58] → 1,014.25 [978.33–1,053.04] | 1,369.21 [1,321.33–2,203.04] → 1,557.46 [1,403.75–2,098.38] | 369.46 [368.5–380.05] → 370.48 [369.99–374.35] | 10 [9.95–10] → 9.8 [9.72–9.84] |
| TCP over TLS · Standalone Asio · 16 connections · 64 B · in flight 1 · threads 1 | 106,106.35 [105,247.71–106,720.52] → 105,305.82 [104,954.37–105,832.33] | 147.92 [147.08–148.63] → 149.04 [148.88–149.38] | 169.08 [168.71–199.5] → 170.96 [170.13–171.04] | 99.01 [99.01–99.12] → 99.21 [99–99.52] | 30.86 [30.73–30.89] → 30.94 [30.88–31.03] |
| TCP over TLS · Standalone Asio · 16 connections · 64 B · in flight 1 · threads 4 | 159,955.53 [158,495.15–161,482.69] → 158,124.81 [157,452.86–162,712.28] | 97.79 [96.63–98.33] → 98.5 [96.92–98.96] | 135 [132.5–136.21] → 136.42 [129.88–138.5] | 288.34 [287.49–288.93] → 289.87 [289.52–290.95] | 31.19 [31.17–31.23] → 31.16 [31.13–31.28] |
| TCP over TLS · Standalone Asio · 16 connections · 64 B · in flight 16 · threads 1 | 129,384.03 [128,210.63–134,639.18] → 133,896.88 [128,653.03–135,326.48] | 1,970.38 [1,815.08–1,980.75] → 1,898.79 [1,866.83–1,975.92] | 2,056.58 [2,046.88–2,946.63] → 2,052.38 [2,035.58–2,074.04] | 94.64 [90.98–98.8] → 97.69 [95.69–98.78] | 30.89 [30.83–31.08] → 31.03 [30.95–31.06] |
| TCP over TLS · Standalone Asio · 16 connections · 64 B · in flight 16 · threads 4 | 242,693.19 [241,041.94–253,251.85] → 249,546.29 [236,231.24–250,812.31] | 1,055.08 [1,004.33–1,056.83] → 1,019.58 [1,013.83–1,081.5] | 1,249.5 [1,247.04–1,321.71] → 1,265.08 [1,229.13–1,734.63] | 368.52 [367.2–370.89] → 371.02 [369.93–371.93] | 31.27 [31.23–31.27] → 31.38 [31.28–31.41] |
| TCP over TLS · Standalone Asio · 16 connections · 4096 B · in flight 1 · threads 1 | 61,010.1 [60,919.92–61,693.92] → 61,011.27 [60,956.79–61,605.08] | 254.75 [253.83–255.46] → 255.13 [254.88–255.96] | 280.75 [280.25–285.04] → 281.42 [280.13–281.92] | 99.01 [99.01–99.79] → 99.32 [99.13–99.87] | 31.91 [31.88–31.97] → 31.94 [31.86–32.2] |
| TCP over TLS · Standalone Asio · 16 connections · 4096 B · in flight 1 · threads 4 | 122,248.16 [122,172.7–122,486.39] → 121,482.7 [121,405.62–121,987.33] | 128.38 [128.17–128.79] → 129.08 [128.63–129.46] | 155.42 [154.25–161.54] → 164.71 [160.5–165.71] | 306.33 [305.78–307.02] → 306.68 [306.28–306.97] | 31.98 [31.94–32.23] → 32.25 [32.13–32.41] |
| TCP over TLS · Standalone Asio · 16 connections · 4096 B · in flight 16 · threads 1 | 68,965.65 [68,737.98–70,858.23] → 69,614.3 [69,172.44–69,729.16] | 3,677.29 [3,596.25–3,717.38] → 3,662.63 [3,647.54–3,665.13] | 3,824.13 [3,757–3,856.96] → 3,771.08 [3,748.25–3,784.13] | 95.38 [94.68–97.67] → 96.98 [96.97–97.71] | 34.88 [34.7–35.06] → 34.91 [34.89–34.94] |
| TCP over TLS · Standalone Asio · 16 connections · 4096 B · in flight 16 · threads 4 | 187,958.75 [186,868.34–191,717.45] → 183,398.34 [180,029.22–184,058.46] | 1,342.92 [1,316.38–1,343.25] → 1,375.92 [1,365.08–1,406.21] | 1,493.04 [1,453.46–2,072.21] → 1,540.42 [1,526.92–1,781.79] | 386.69 [386.38–389.56] → 386.68 [383.94–387.23] | 35 [34.92–35.09] → 35.03 [35–35.19] |

</details>

## Reproduce

[Build both library revisions with the same benchmark drivers](../../benchmarks/comparison/README.md).

[Build configuration and parameters](methodology.md). Run the standalone C++ benchmark programs and keep measured output outside the source directory.

```sh
cmake -S . -B build/benchmark -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build build/benchmark --target arknet_loopback_benchmark
./build/benchmark/benchmarks/arknet_loopback_benchmark --protocol tcp --payload 64 --clients 16 \
  --window 16 --io-model shared --io-threads 4 --work 0 \
  --warmup 0.25 --seconds 1
```
