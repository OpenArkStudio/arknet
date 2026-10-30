# Asio callbacks, coroutines and IO models

16 TCP connections: three IO models, callbacks/coroutines, 64 B / 4096 B payloads, 1 / 16 outstanding requests per connection, 0 / 10,000 iterations per echo. Both dependencies total 96 configurations × three independent processes: 288 measurements, zero content-validation or IO errors.

<Aside type="note" title="Measurement scope">

This native Asio TCP echo benchmark measures execution and scheduling costs. The client and server share one process and the configured IO workers. Public arknet endpoint overhead, cross-machine capacity and acceptance at 100,000–200,000 PCU require separate tests.

</Aside>

## Test environment

| Item | Configuration |
| --- | --- |
| Machine | Mac mini (Mac16,11) · Apple M4 Pro · 14 physical / 14 logical cores · 48 GiB |
| OS | macOS 27.0.1 · build 26A434 · arm64 |
| Compiler and flags | Apple clang 21.0.0 (clang-2100.3.34.2) · C++20 · Release · -O3 -DNDEBUG |
| Dependencies | Boost.Asio 1.90 / Standalone Asio 1.38.2; TLS disabled |
| Source | arknet v0.2.2 · `c5432ca176edf9f4c06db03713f03764184c5cea` |
| Started | 2026-10-17 UTC · Standalone 14:26:42 · Boost 15:02:30 |
| Each run | 0.5 s warmup + 2 s traffic; three independent processes per configuration |

## IO models

| Chart label | io_context objects | Total IO workers | Scheduling |
| --- | ---: | ---: | --- |
| 1 context / 1 thread | 1 | 1 | One thread runs all connections |
| 1 context / 4 threads | 1 | 4 | Four threads run the same event loop |
| 4 contexts / 4 threads | 4 | 4 | One thread per event loop; connections assigned round-robin |

Each connection serializes its handlers through a strand. The one-context, one-thread configuration is measured once rather than duplicated under a sharded label.

## Throughput, CPU and response time

Bars show three independent processes’ median; whiskers show min/max. Throughput counts completed TCP request–echo round trips/s, each once. Click for the full SVG.

CPU combines user and system CPU for the process containing both client and server, including warmup and traffic; 100% means one saturated logical core. Throughput excludes warmup and uses the actual measured duration, including completion of the final outstanding batch.

<Aside type="note" title="Response-time scope">

Window 1 measures message RTT; window 16 measures the batch of 16 requests. Dividing by 16 does not yield message RTT. Each connection retains up to 8192 reservoir samples; combined-sample P99 is estimated. Tables summarize three per-run P99 values, not all requests’ global P99.

</Aside>

### Boost.Asio 1.90

#### No additional computation

[![Boost.Asio 1.90 · No additional computation · Throughput](assets/asio-022-boost-work0-throughput.svg)](assets/asio-022-boost-work0-throughput.svg)

[![Boost.Asio 1.90 · No additional computation · CPU utilization](assets/asio-022-boost-work0-cpu.svg)](assets/asio-022-boost-work0-cpu.svg)

**P99 response time**

[![Boost.Asio 1.90 · No additional computation · P99](assets/asio-022-boost-work0-p99.svg)](assets/asio-022-boost-work0-p99.svg)


#### 10,000 computation iterations per echo

[![Boost.Asio 1.90 · 10,000 computation iterations per echo · Throughput](assets/asio-022-boost-work10000-throughput.svg)](assets/asio-022-boost-work10000-throughput.svg)

[![Boost.Asio 1.90 · 10,000 computation iterations per echo · CPU utilization](assets/asio-022-boost-work10000-cpu.svg)](assets/asio-022-boost-work10000-cpu.svg)

**P99 response time**

[![Boost.Asio 1.90 · 10,000 computation iterations per echo · P99](assets/asio-022-boost-work10000-p99.svg)](assets/asio-022-boost-work10000-p99.svg)


### Standalone Asio 1.38.2

#### No additional computation

[![Standalone Asio 1.38.2 · No additional computation · Throughput](assets/asio-022-standalone-work0-throughput.svg)](assets/asio-022-standalone-work0-throughput.svg)

[![Standalone Asio 1.38.2 · No additional computation · CPU utilization](assets/asio-022-standalone-work0-cpu.svg)](assets/asio-022-standalone-work0-cpu.svg)

**P99 response time**

[![Standalone Asio 1.38.2 · No additional computation · P99](assets/asio-022-standalone-work0-p99.svg)](assets/asio-022-standalone-work0-p99.svg)


#### 10,000 computation iterations per echo

[![Standalone Asio 1.38.2 · 10,000 computation iterations per echo · Throughput](assets/asio-022-standalone-work10000-throughput.svg)](assets/asio-022-standalone-work10000-throughput.svg)

[![Standalone Asio 1.38.2 · 10,000 computation iterations per echo · CPU utilization](assets/asio-022-standalone-work10000-cpu.svg)](assets/asio-022-standalone-work10000-cpu.svg)

**P99 response time**

[![Standalone Asio 1.38.2 · 10,000 computation iterations per echo · P99](assets/asio-022-standalone-work10000-p99.svg)](assets/asio-022-standalone-work10000-p99.svg)


The 10,000 deterministic iterations execute inside the IO handler. This is neither a separate compute pool nor a fixed amount of business processing time.

## Choosing a model

The coroutine throughput change relative to callbacks spans the following ranges across 24 matched configurations per dependency.

| Dependency | Throughput change range |
| --- | ---: |
| Boost.Asio 1.90 | -4.76% to +13.08% |
| Standalone Asio 1.38.2 | -4.92% to +2.5% |

| Workload | Starting model | Check |
| --- | --- | --- |
| Few connections with light IO processing | One event loop, one worker | Throughput and tail latency when CPU saturates |
| Uneven connection workloads scheduled by several workers | One event loop, multiple workers | Strand scope and shared scheduling cost |
| Connections assignable to explicit instances or shards | Multiple event loops, one worker each | Shard imbalance and cross-shard communication |
| Expensive parsing, compression or computation | A bounded compute executor alongside the IO model | Coroutines do not automatically move computation to another thread |

<Aside type="caution" title="Selection limits">

These starting points use 16 balanced connections in a short closed-loop local run; they establish neither production capacity nor a universal best model. Three-run ranges are not confidence intervals.

</Aside>

## Full results

**Boost.Asio 1.90 · 48 configurations: median [minimum–maximum]**

| Payload B | Window | Iterations | Contexts / total workers | API | Round trips/s | P50 µs | P99 µs | CPU% |
| ---: | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: |
| 64 | 1 | 0 | 1 / 1 | callback | 202,549.03 [200,318.3–211,068.46] | 78.67 [74.08–79] | 101.92 [100.21–102.38] | 99.93 [99.91–99.93] |
| 64 | 1 | 0 | 1 / 1 | coroutine | 192,908.77 [191,813.92–201,684.79] | 82.38 [77.92–82.67] | 104.79 [102.88–105.58] | 99.93 [99.92–99.98] |
| 64 | 16 | 0 | 1 / 1 | callback | 2,718,624.66 [2,706,816.9–2,860,913.74] | 92.54 [87–92.88] | 113.79 [109.54–114.29] | 99.93 [99.93–99.97] |
| 64 | 16 | 0 | 1 / 1 | coroutine | 2,607,581.61 [2,599,330.86–2,704,442.47] | 96.5 [90.63–96.54] | 118.54 [118.46–133.29] | 99.94 [99.93–99.94] |
| 4096 | 1 | 0 | 1 / 1 | callback | 120,089.98 [119,862.87–126,603.94] | 128.75 [119.58–128.88] | 153.42 [153.13–160.54] | 99.93 [99.92–99.95] |
| 4096 | 1 | 0 | 1 / 1 | coroutine | 115,924.14 [114,777.34–122,488.67] | 133.67 [124.92–134.21] | 160.46 [159.04–161.08] | 99.93 [99.92–99.93] |
| 4096 | 16 | 0 | 1 / 1 | callback | 283,672.87 [281,819.77–291,110.62] | 856.42 [830.54–862.58] | 945.92 [915.67–966.96] | 99.94 [99.94–99.95] |
| 4096 | 16 | 0 | 1 / 1 | coroutine | 282,038.86 [281,657.55–291,921.09] | 861.83 [830.5–866.04] | 934.79 [923.71–974] | 99.98 [99.97–99.98] |
| 64 | 1 | 10000 | 1 / 1 | callback | 47,573.53 [47,067.1–47,980.19] | 332 [331.38–333.21] | 380.54 [364.13–405.54] | 99.93 [99.85–99.93] |
| 64 | 1 | 10000 | 1 / 1 | coroutine | 46,829.99 [46,725.21–46,959.32] | 339.25 [339–339.54] | 379.33 [371.08–382.63] | 99.92 [99.9–99.93] |
| 64 | 16 | 10000 | 1 / 1 | callback | 69,392.85 [68,731.6–69,570.31] | 3,661.42 [3,657.67–3,723.08] | 3,887.96 [3,854.25–3,913.67] | 99.94 [99.94–99.99] |
| 64 | 16 | 10000 | 1 / 1 | coroutine | 68,936.11 [68,835.69–69,123.92] | 3,700.88 [3,692.46–3,713.71] | 4,065.13 [3,970.83–4,089.67] | 99.96 [99.92–99.97] |
| 4096 | 1 | 10000 | 1 / 1 | callback | 41,029.93 [40,217.31–41,616.66] | 380.67 [379.58–383.63] | 432.33 [420.63–503.75] | 99.94 [99.71–99.96] |
| 4096 | 1 | 10000 | 1 / 1 | coroutine | 40,958.15 [40,542.15–41,118.95] | 385.42 [382.75–388.38] | 433.54 [430.58–435.83] | 99.94 [99.89–99.97] |
| 4096 | 16 | 10000 | 1 / 1 | callback | 57,347.67 [57,021.83–57,462.17] | 4,418 [4,400.46–4,440.71] | 4,638.33 [4,544.67–4,742.25] | 99.95 [99.95–99.99] |
| 4096 | 16 | 10000 | 1 / 1 | coroutine | 56,998.17 [56,469.49–57,151.63] | 4,424.83 [4,412.38–4,440.92] | 4,921.54 [4,798.71–4,957.96] | 99.99 [99.98–99.99] |
| 64 | 1 | 0 | 1 / 4 | callback | 205,487.16 [205,058.26–206,165.46] | 79.33 [78.67–79.42] | 106.83 [104.71–114.42] | 257.81 [257.78–257.94] |
| 64 | 1 | 0 | 1 / 4 | coroutine | 204,318.22 [201,388.89–204,587.27] | 79.75 [79.67–80] | 107.17 [106.54–132.42] | 261.24 [260.36–261.79] |
| 64 | 16 | 0 | 1 / 4 | callback | 3,118,378.94 [3,110,792.05–3,133,405.13] | 82.92 [82.63–83.08] | 108.25 [106.83–110.58] | 260.4 [260.09–260.77] |
| 64 | 16 | 0 | 1 / 4 | coroutine | 3,089,399.48 [3,066,930.55–3,108,741.04] | 83.5 [83.46–83.83] | 124.92 [108.58–130.88] | 264.28 [263.8–264.88] |
| 4096 | 1 | 0 | 1 / 4 | callback | 172,907.03 [165,751.29–173,587.72] | 90.25 [90.21–91.13] | 122.79 [121.08–167.63] | 268.98 [268.22–269.57] |
| 4096 | 1 | 0 | 1 / 4 | coroutine | 171,562.05 [169,816.64–173,099] | 90.83 [90.42–91] | 121.58 [121.04–149.08] | 272.54 [272–273.7] |
| 4096 | 16 | 0 | 1 / 4 | callback | 828,832.05 [736,977.59–831,222.44] | 279.29 [275.71–302.5] | 319.33 [314.5–416.42] | 328.34 [316.03–329.14] |
| 4096 | 16 | 0 | 1 / 4 | coroutine | 797,701.43 [769,596.39–830,535.34] | 278.08 [276.83–289.33] | 382.75 [310.79–407.79] | 330.81 [325.63–331.76] |
| 64 | 1 | 10000 | 1 / 4 | callback | 115,709.12 [115,495.81–115,924.79] | 135.08 [134.92–135.38] | 165.46 [164.67–172.96] | 343.76 [343.07–343.78] |
| 64 | 1 | 10000 | 1 / 4 | coroutine | 115,677.3 [107,739.62–115,819.79] | 135.79 [135.46–142.54] | 173.67 [168.08–287.17] | 346.61 [339.42–347.29] |
| 64 | 16 | 10000 | 1 / 4 | callback | 241,659.2 [241,355.26–242,094.99] | 1,056.17 [1,052.13–1,060.92] | 1,591.04 [1,511.17–1,626.67] | 397.73 [397.24–398.05] |
| 64 | 16 | 10000 | 1 / 4 | coroutine | 240,456.58 [240,347.5–240,589.02] | 1,060.83 [1,059–1,066.42] | 1,630.13 [1,389.92–1,661.13] | 397.35 [396.71–398.06] |
| 4096 | 1 | 10000 | 1 / 4 | callback | 106,133.6 [104,782.48–106,271.69] | 147 [146.88–147.83] | 186.63 [186.63–207.13] | 346.67 [345.21–346.76] |
| 4096 | 1 | 10000 | 1 / 4 | coroutine | 105,823.5 [104,541.87–105,916.83] | 147.71 [147.54–148.29] | 186.46 [184.83–207.83] | 348.69 [347.23–349.49] |
| 4096 | 16 | 10000 | 1 / 4 | callback | 195,346.95 [195,079.6–195,356.58] | 1,241.04 [1,236.46–1,241.79] | 1,592.63 [1,571.75–1,667.42] | 396.45 [396.24–396.52] |
| 4096 | 16 | 10000 | 1 / 4 | coroutine | 195,688.87 [195,070.06–195,873.92] | 1,236 [1,235.21–1,244.71] | 1,620.13 [1,593.71–1,639.75] | 396.1 [394.8–396.35] |
| 64 | 1 | 0 | 4 / 4 | callback | 336,353.3 [303,989.43–342,492.6] | 39.54 [39.13–40.92] | 109.08 [107.96–117.25] | 387.72 [386.45–390.23] |
| 64 | 1 | 0 | 4 / 4 | coroutine | 352,288.97 [313,011.27–360,948.57] | 39.25 [39–40.83] | 101.63 [101.29–112.17] | 384.52 [379.49–385.4] |
| 64 | 16 | 0 | 4 / 4 | callback | 4,916,757.2 [4,901,549.8–5,381,788.14] | 40.63 [39.67–40.88] | 110.13 [108–113.13] | 385.06 [380.09–385.93] |
| 64 | 16 | 0 | 4 / 4 | coroutine | 5,559,668.13 [4,797,376.5–5,608,827.35] | 40 [40–41.92] | 102.46 [101.13–114.63] | 385.13 [381.03–385.87] |
| 4096 | 1 | 0 | 4 / 4 | callback | 283,846.32 [258,814.36–313,521.59] | 44.88 [43.33–47.5] | 111.67 [101.71–118.42] | 388.07 [383.37–388.55] |
| 4096 | 1 | 0 | 4 / 4 | coroutine | 299,032.51 [227,628.56–302,278.13] | 44.33 [44.21–68] | 103.96 [98.96–122.67] | 386.41 [381.44–388.99] |
| 4096 | 16 | 0 | 4 / 4 | callback | 1,027,155.34 [1,019,387.36–1,042,506.1] | 203.63 [203.54–209.58] | 267.21 [252.17–335.29] | 399.13 [398.51–399.58] |
| 4096 | 16 | 0 | 4 / 4 | coroutine | 1,032,032.95 [1,025,657.56–1,035,007.83] | 205.54 [204.21–206.08] | 281.88 [281.42–283.46] | 399.1 [398.89–399.73] |
| 64 | 1 | 10000 | 4 / 4 | callback | 156,410.76 [156,200.88–158,004.83] | 100.46 [99.75–100.71] | 140.96 [140.58–145.13] | 399.28 [399.25–399.5] |
| 64 | 1 | 10000 | 4 / 4 | coroutine | 156,108.21 [146,057.84–156,862.76] | 100.79 [100.63–105.58] | 142.42 [139.21–158.96] | 399.26 [398.61–399.39] |
| 64 | 16 | 10000 | 4 / 4 | callback | 246,351.74 [244,755.84–246,709.61] | 1,040.71 [1,040.33–1,050.25] | 1,337.21 [1,299.83–1,358.17] | 399.64 [399.54–399.65] |
| 64 | 16 | 10000 | 4 / 4 | coroutine | 246,033.47 [245,798.89–246,727.23] | 1,043.38 [1,040.58–1,051.5] | 1,308.58 [1,302.92–1,343.46] | 399.76 [399.75–399.76] |
| 4096 | 1 | 10000 | 4 / 4 | callback | 137,508.9 [136,576.49–137,746.04] | 112.17 [112.17–112.58] | 154.75 [153.79–155.58] | 399.55 [399.55–399.72] |
| 4096 | 1 | 10000 | 4 / 4 | coroutine | 136,119.38 [134,870.45–136,977.93] | 113.5 [113–113.92] | 155.5 [153.83–158.96] | 399.29 [398.77–399.53] |
| 4096 | 16 | 10000 | 4 / 4 | callback | 200,057.67 [199,275.15–200,472.99] | 1,258.67 [1,258.21–1,259.42] | 1,313.21 [1,307.38–1,361.25] | 399.7 [399.62–399.78] |
| 4096 | 16 | 10000 | 4 / 4 | coroutine | 199,521.34 [197,414.18–200,107.95] | 1,257.54 [1,254.58–1,262.29] | 1,493.13 [1,465.75–1,527.92] | 399.59 [399.04–399.8] |


**Standalone Asio 1.38.2 · 48 configurations: median [minimum–maximum]**

| Payload B | Window | Iterations | Contexts / total workers | API | Round trips/s | P50 µs | P99 µs | CPU% |
| ---: | ---: | ---: | --- | --- | ---: | ---: | ---: | ---: |
| 64 | 1 | 0 | 1 / 1 | callback | 201,757.6 [198,643.05–214,086.01] | 78.71 [73.96–78.96] | 99.79 [94.54–127.88] | 99.89 [99.86–99.92] |
| 64 | 1 | 0 | 1 / 1 | coroutine | 193,624.79 [192,769.81–206,003.3] | 82.17 [76.67–82.29] | 104.42 [97.96–107.04] | 99.92 [99.9–99.93] |
| 64 | 16 | 0 | 1 / 1 | callback | 2,737,303.88 [2,707,938.25–2,887,186.83] | 91.75 [86.46–92.67] | 114.33 [106.92–115.17] | 99.93 [99.91–99.96] |
| 64 | 16 | 0 | 1 / 1 | coroutine | 2,602,612.74 [2,591,138.38–2,617,045.91] | 95.46 [91.17–96.46] | 144.29 [118.25–164.71] | 99.91 [99.88–99.93] |
| 4096 | 1 | 0 | 1 / 1 | callback | 120,833.42 [119,906.53–128,097.21] | 127.88 [118.04–128.5] | 153.75 [152.17–161.17] | 99.92 [99.91–99.93] |
| 4096 | 1 | 0 | 1 / 1 | coroutine | 115,615.38 [114,373.21–123,795.83] | 131.46 [123.04–133.29] | 159.92 [159.79–217.38] | 99.92 [99.31–99.93] |
| 4096 | 16 | 0 | 1 / 1 | callback | 285,068.49 [283,841.74–293,585.58] | 852.79 [819.71–858.58] | 931.5 [913.75–984.33] | 99.95 [99.81–99.98] |
| 4096 | 16 | 0 | 1 / 1 | coroutine | 278,109.32 [276,886.54–284,101.01] | 859.04 [856.5–870.04] | 1,028.42 [942.46–1,050.17] | 99.91 [99.81–99.99] |
| 64 | 1 | 10000 | 1 / 1 | callback | 47,825.41 [46,291.47–47,851.21] | 332.21 [331.88–337.63] | 366.83 [366.71–414.08] | 99.94 [99.83–99.98] |
| 64 | 1 | 10000 | 1 / 1 | coroutine | 46,844.11 [46,410.92–46,900.88] | 338.75 [338.46–339] | 380.46 [379.21–397.42] | 99.93 [99.91–99.93] |
| 64 | 16 | 10000 | 1 / 1 | callback | 68,864.38 [68,581.97–69,144.93] | 3,714 [3,669.92–3,735.79] | 3,864.21 [3,856.21–4,308.79] | 99.94 [99.9–99.99] |
| 64 | 16 | 10000 | 1 / 1 | coroutine | 68,614.14 [68,578.59–68,758.03] | 3,732.29 [3,728.79–3,734.25] | 3,995.38 [3,839.83–4,036.29] | 99.98 [99.98–99.99] |
| 4096 | 1 | 10000 | 1 / 1 | callback | 41,551.54 [41,223.88–41,594.86] | 379.29 [379.21–379.92] | 425.63 [415.08–435.92] | 99.97 [99.91–99.97] |
| 4096 | 1 | 10000 | 1 / 1 | coroutine | 41,194.68 [40,447.15–41,286.38] | 383.29 [382.29–388.08] | 428.13 [420.92–446] | 99.98 [99.96–99.98] |
| 4096 | 16 | 10000 | 1 / 1 | callback | 57,161.07 [56,910.36–57,618.63] | 4,422.04 [4,399.54–4,452.63] | 4,660.38 [4,647.21–4,874.04] | 99.95 [99.91–99.98] |
| 4096 | 16 | 10000 | 1 / 1 | coroutine | 57,317.81 [56,990.89–57,597.06] | 4,408.54 [4,392.83–4,444.54] | 4,767 [4,677.46–5,023] | 99.98 [99.98–99.98] |
| 64 | 1 | 0 | 1 / 4 | callback | 205,879.62 [203,359.41–206,216.68] | 79.08 [79–79.5] | 105.21 [103.58–123.83] | 257.16 [256.66–257.69] |
| 64 | 1 | 0 | 1 / 4 | coroutine | 205,223.88 [202,356.67–205,285.2] | 79.58 [79.42–80] | 108.92 [104.92–124.75] | 260.89 [260.43–261.26] |
| 64 | 16 | 0 | 1 / 4 | callback | 3,096,862.51 [2,640,214.18–3,120,151.33] | 83.33 [82.83–89.58] | 113.08 [110.42–164.29] | 259.93 [258.24–261.02] |
| 64 | 16 | 0 | 1 / 4 | coroutine | 3,064,338.2 [3,022,524–3,109,969.31] | 83.5 [83.08–84.04] | 136.71 [119.54–143.21] | 263.08 [262.85–264.74] |
| 4096 | 1 | 0 | 1 / 4 | callback | 171,416.46 [158,950.18–172,228.67] | 90.92 [89.83–92.46] | 143.79 [122.42–168.96] | 268.72 [267.11–269.07] |
| 4096 | 1 | 0 | 1 / 4 | coroutine | 168,985.47 [162,136.28–173,049.81] | 91.5 [90.42–92] | 142.63 [118.38–174.46] | 271.78 [270.85–272.04] |
| 4096 | 16 | 0 | 1 / 4 | callback | 821,564.08 [796,175.45–822,369.88] | 283.83 [282.75–286.92] | 321.42 [313.46–363.04] | 325.49 [325.27–326.7] |
| 4096 | 16 | 0 | 1 / 4 | coroutine | 827,711.91 [811,171.51–832,033.78] | 279.83 [276.71–280.83] | 313.08 [311.25–323.71] | 330.71 [328.9–332.21] |
| 64 | 1 | 10000 | 1 / 4 | callback | 116,277.55 [109,282.07–116,417.22] | 134.5 [134.46–138.75] | 172.92 [166.13–310.33] | 344.08 [335.82–344.78] |
| 64 | 1 | 10000 | 1 / 4 | coroutine | 116,005.41 [109,714.62–116,579.5] | 135.42 [134.58–139.83] | 172.21 [166.71–222.79] | 347 [341.28–347.26] |
| 64 | 16 | 10000 | 1 / 4 | callback | 241,200.22 [240,746.04–241,630.9] | 1,058.21 [1,056.08–1,064] | 1,536.08 [1,487.58–1,554.46] | 397.2 [397.17–397.75] |
| 64 | 16 | 10000 | 1 / 4 | coroutine | 241,731.67 [240,331.95–241,747.76] | 1,054.96 [1,052.54–1,064.38] | 1,604.58 [1,542.67–1,630.67] | 397.85 [396.92–398.18] |
| 4096 | 1 | 10000 | 1 / 4 | callback | 103,909.86 [99,020.99–106,099.45] | 148.58 [146.96–154.33] | 226.58 [187.08–235.25] | 343.94 [335.91–345.93] |
| 4096 | 1 | 10000 | 1 / 4 | coroutine | 102,808.97 [99,984.56–105,827.4] | 149.67 [147.46–152.83] | 221.79 [193.71–230.88] | 344.66 [336.03–348.91] |
| 4096 | 16 | 10000 | 1 / 4 | callback | 195,588.46 [195,351.32–195,862.35] | 1,239.71 [1,233.58–1,244.42] | 1,637.54 [1,596–1,641.13] | 396.23 [394.99–396.25] |
| 4096 | 16 | 10000 | 1 / 4 | coroutine | 195,251.99 [195,223.67–195,822.7] | 1,237.29 [1,235.46–1,239] | 1,637.79 [1,629.25–1,644.54] | 396.3 [395.55–396.34] |
| 64 | 1 | 0 | 4 / 4 | callback | 362,569.85 [340,929.06–362,675.12] | 39.04 [38.54–39.46] | 100.79 [100.04–108.13] | 382.13 [381.05–389.13] |
| 64 | 1 | 0 | 4 / 4 | coroutine | 357,514.25 [349,431.87–371,997.33] | 39.08 [38.79–39.5] | 101.42 [98.08–107.25] | 384.46 [382.15–389.52] |
| 64 | 16 | 0 | 4 / 4 | callback | 5,515,096.23 [5,288,557.92–5,645,962.84] | 39.83 [39.71–40.29] | 105.25 [102.75–108.04] | 384.65 [383.69–389.76] |
| 64 | 16 | 0 | 4 / 4 | coroutine | 5,341,884.43 [5,166,499.51–5,353,263.72] | 40.54 [40.54–41.33] | 107.75 [104.21–111] | 385.97 [382.45–388.78] |
| 4096 | 1 | 0 | 4 / 4 | callback | 298,801.44 [288,770.76–321,225.93] | 43.79 [42.88–44.25] | 107.29 [99.46–112.17] | 386.8 [386.63–387.65] |
| 4096 | 1 | 0 | 4 / 4 | coroutine | 306,280.25 [301,725.41–318,831.36] | 44.29 [44.04–44.33] | 100.92 [99.42–102.71] | 386.64 [381.96–390.2] |
| 4096 | 16 | 0 | 4 / 4 | callback | 1,050,625.55 [1,048,673.66–1,051,600.7] | 202.54 [202.29–202.75] | 247.92 [244.71–258] | 399.67 [399.6–399.72] |
| 4096 | 16 | 0 | 4 / 4 | coroutine | 1,035,727.28 [1,033,586.06–1,044,536.73] | 204.33 [202.83–205.42] | 283.67 [280.04–289.5] | 399.61 [399.17–399.66] |
| 64 | 1 | 10000 | 4 / 4 | callback | 157,225.97 [153,783.79–157,303.86] | 100.17 [100.13–101.54] | 142.92 [141.04–148.25] | 399.37 [399.3–399.39] |
| 64 | 1 | 10000 | 4 / 4 | coroutine | 156,194.05 [153,475.58–156,335.04] | 101.17 [101–101.71] | 140.79 [140.75–149.83] | 399.37 [396.46–399.56] |
| 64 | 16 | 10000 | 4 / 4 | callback | 246,062.41 [245,927.05–247,416.06] | 1,044.88 [1,038.96–1,048.33] | 1,316.38 [1,309.83–1,321.33] | 399.55 [399.52–399.55] |
| 64 | 16 | 10000 | 4 / 4 | coroutine | 246,338.94 [245,931.04–246,510.54] | 1,049.13 [1,040.5–1,051.5] | 1,319.88 [1,317.25–1,353.71] | 399.36 [399.34–399.75] |
| 4096 | 1 | 10000 | 4 / 4 | callback | 137,318.5 [137,275.42–138,656.51] | 112.29 [111.63–112.38] | 154.79 [149.63–156.42] | 399.47 [398.42–399.51] |
| 4096 | 1 | 10000 | 4 / 4 | coroutine | 135,581.78 [130,649.39–135,932.83] | 113.67 [113.58–116.79] | 158.88 [156.96–164.83] | 399.27 [399.19–399.59] |
| 4096 | 16 | 10000 | 4 / 4 | callback | 200,877.78 [200,853.23–201,365.33] | 1,255.54 [1,255.21–1,256.79] | 1,321.63 [1,306.17–1,418.79] | 399.59 [399.56–399.64] |
| 4096 | 16 | 10000 | 4 / 4 | coroutine | 200,758.38 [198,967.33–201,314.41] | 1,255.58 [1,252.96–1,260.71] | 1,541.13 [1,482.96–1,876.13] | 399.58 [398.55–399.7] |


## Reproduction

<Steps>

<Step title="Run one benchmark">

Run the repository’s C++ benchmark directly, without Python, Ruby or Node.js. This example defaults to Boost.

```sh
cmake -S . -B build/benchmark -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_EXTENSIONS=OFF -DARKNET_BUILD_TESTS=OFF \
  -DARKNET_BUILD_EXAMPLES=OFF -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=OFF
cmake --build build/benchmark --target arknet_coroutine_benchmark

./build/benchmark/benchmarks/arknet_coroutine_benchmark \
  --protocol tcp --execution coroutine --payload 4096 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --work 10000 --warmup 0.5 --seconds 2
```

For Standalone Asio, use a separate build directory and these options:

```sh
-DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
```

</Step>

<Step title="Run the matrix">

The maintainer batch script additionally requires jq and GNU timeout. Point `--executable` at the matching build and use `--backend standalone` for Standalone Asio.

```sh
bash scripts/benchmark_matrix.sh \
  --executable build/benchmark/benchmarks/arknet_coroutine_benchmark \
  --certs tests/certs --output /tmp/arknet-asio-thread-models-boost.json \
  --backend boost --address-family ipv4 --protocols tcp --clients 16 \
  --payloads 64 4096 --windows 1 16 --io-models shared sharded --io-threads 1 4 \
  --execution-modes callback coroutine --work-values 0 10000 \
  --repetitions 3 --seconds 2 --warmup 0.5
```

</Step>

<Step title="Retain environment and local records">

The macOS measurements use Command Line Tools AppleClang and an explicit SDK: `-DCMAKE_CXX_COMPILER=/Library/Developer/CommandLineTools/usr/bin/clang++ -DCMAKE_OSX_SYSROOT="$(xcrun --show-sdk-path)"`. CPU affinity and frequency were not pinned. These results do not validate Homebrew LLVM 22.

Raw measurements and build logs remain in local acceptance records; they are not included in source or offered as public downloads. SHA256 digests of the measurement files used here:

- Boost.Asio 1.90: `1e46085250ecf82d5eec992948cad094ecc4a0512060b9d03249822ba3683f53`
- Standalone Asio 1.38.2: `fe40cc8f647d3b3f877751456dae2c45df60331f02063448eba92afabf971a7c`

Benchmark source: [coroutine.cpp](https://github.com/OpenArkStudio/arknet/blob/c5432ca176edf9f4c06db03713f03764184c5cea/benchmarks/coroutine.cpp) · [runtime.hpp](https://github.com/OpenArkStudio/arknet/blob/c5432ca176edf9f4c06db03713f03764184c5cea/benchmarks/runtime.hpp). [Metric definitions and other protocol tests](methodology.md).

</Step>

</Steps>
