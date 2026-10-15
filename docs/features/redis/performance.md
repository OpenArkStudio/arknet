# Redis Performance

Bars show round medians; whiskers show minimum–maximum. Latency summaries are medians of the per-round percentiles.

## Test environment

| Item | Configuration |
| --- | --- |
| Measurement (UTC) | 2026-10-15T13:29:26Z – 2026-10-15T13:34:33Z |
| Machine | Mac16,11 · Apple M4 Pro · arm64 |
| CPU (physical / logical cores) | 14 / 14 |
| Memory | 48 GiB |
| Operating system | macOS 27.0.1 (26A434) |
| Compiler | Apple Clang 21.0.0 (clang-2100.3.34.2) |
| Build | Release · -O3 -DNDEBUG · C++20 |
| Server | Valkey 8.0.11 · RESP3 · AUTH=ON · AOF/RDB=OFF |
| Dependencies | Boost 1.90.0 / Standalone Asio 1.38.2 / OpenSSL 3.6.4 |
| Connections | 4 |
| Payload | 1 / 4 / 16 KiB |
| Commands per batch | 1 / 4 / 16 / 64 |
| Batches in flight per connection | 1 / 4 |
| Transport | TCP / TLS · RESP3 · ECHO |
| Source commit | c6cacab764e83dbd52e0cb42acd5cc534597184f |
| Official server source SHA-256 | e4ee4ad51c2e9cb75c5fac8865d34792482a597bf3b136b42b296c9933bd31f8 |
| Warmup / target duration / rounds | 0.15 s / 1 s / 3 |
| Workload combinations | 96 |

Client and Valkey server run on this machine over IPv4 loopback. TLS certificate-chain and IP identity verification are enabled. Client CPU excludes the server process. Other local builds and tests were paused during measurement.

## Measurement scope

Binary ECHO with every reply verified; callback API, one IO context/thread per connection. RT measures the entire pipeline batch including client queueing, not individual command latency.

CPU is client process usage with one full core at 100%; it can exceed 100% and excludes the database server. RSS is the process lifetime peak, including warmup and earlier rounds, not newly allocated memory in that round.

Every group retains at least 368 latency samples per round. Bounded sampling estimates percentiles. Local results do not establish production deployment capacity.

## Command throughput

![Command throughput](../../performance/assets/redis-throughput.svg)

## Pipeline batch RT p50

![Pipeline batch RT p50](../../performance/assets/redis-p50.svg)

## Pipeline batch RT p99

![Pipeline batch RT p99](../../performance/assets/redis-p99.svg)

## Client CPU

![Client CPU](../../performance/assets/redis-cpu.svg)

## Client process peak RSS

![Client process peak RSS](../../performance/assets/redis-rss.svg)

## Detailed results

<details>
<summary>Expand median results</summary>

| Dependency / transport / mode | Payload B | Clients | Commands/batch / batches in-flight | Commands/s | Batch p50 µs | Batch p99 µs | CPU % | RSS MiB | Rounds |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio · TCP | 1024 | 4 | 1 / 1 | 93,771.1 | 41.67 | 69.67 | 139.12 | 8.98 | 3 |
| Boost.Asio · TLS | 1024 | 4 | 1 / 1 | 81,439.22 | 47.96 | 77.08 | 144.08 | 15.67 | 3 |
| Standalone Asio · TCP | 1024 | 4 | 1 / 1 | 94,508.09 | 41.42 | 67.33 | 138.4 | 8.97 | 3 |
| Standalone Asio · TLS | 1024 | 4 | 1 / 1 | 80,727.17 | 48.5 | 80.04 | 144.75 | 16.41 | 3 |
| Boost.Asio · TCP | 1024 | 4 | 4 / 1 | 219,694.29 | 71.88 | 90.04 | 124.37 | 8 | 3 |
| Boost.Asio · TLS | 1024 | 4 | 4 / 1 | 192,825.71 | 81.83 | 109.29 | 130.93 | 15.47 | 3 |
| Standalone Asio · TCP | 1024 | 4 | 4 / 1 | 215,414.9 | 72.67 | 109.42 | 126.41 | 9.77 | 3 |
| Standalone Asio · TLS | 1024 | 4 | 4 / 1 | 192,532.09 | 82 | 108.58 | 131.51 | 16.05 | 3 |
| Boost.Asio · TCP | 1024 | 4 | 16 / 1 | 322,188.92 | 197.21 | 239.13 | 104.97 | 7.72 | 3 |
| Boost.Asio · TLS | 1024 | 4 | 16 / 1 | 277,446.83 | 229.17 | 283.25 | 121.59 | 15.23 | 3 |
| Standalone Asio · TCP | 1024 | 4 | 16 / 1 | 322,729.57 | 197 | 231.08 | 104.63 | 7.22 | 3 |
| Standalone Asio · TLS | 1024 | 4 | 16 / 1 | 279,482.47 | 227.96 | 273.92 | 120.23 | 15.09 | 3 |
| Boost.Asio · TCP | 1024 | 4 | 64 / 1 | 383,950.44 | 664.79 | 726.42 | 102 | 7.94 | 3 |
| Boost.Asio · TLS | 1024 | 4 | 64 / 1 | 334,296.89 | 757.29 | 876.29 | 120.05 | 15.39 | 3 |
| Standalone Asio · TCP | 1024 | 4 | 64 / 1 | 363,308.77 | 704 | 755.63 | 102.55 | 8.02 | 3 |
| Standalone Asio · TLS | 1024 | 4 | 64 / 1 | 337,511.19 | 755.38 | 843.83 | 118.5 | 15.59 | 3 |
| Boost.Asio · TCP | 1024 | 4 | 1 / 4 | 205,768.14 | 80.46 | 116.83 | 185.82 | 10.27 | 3 |
| Boost.Asio · TLS | 1024 | 4 | 1 / 4 | 178,333.3 | 89.79 | 119.58 | 202.62 | 16.66 | 3 |
| Standalone Asio · TCP | 1024 | 4 | 1 / 4 | 209,150.96 | 78.63 | 116.38 | 186.2 | 9.19 | 3 |
| Standalone Asio · TLS | 1024 | 4 | 1 / 4 | 176,416.18 | 90.17 | 121.29 | 203.21 | 16.61 | 3 |
| Boost.Asio · TCP | 1024 | 4 | 4 / 4 | 414,659.72 | 152.96 | 198.79 | 165.96 | 9.38 | 3 |
| Boost.Asio · TLS | 1024 | 4 | 4 / 4 | 322,773.09 | 196.96 | 251.04 | 172.65 | 18.25 | 3 |
| Standalone Asio · TCP | 1024 | 4 | 4 / 4 | 415,810.32 | 152.54 | 198.96 | 165.58 | 9.27 | 3 |
| Standalone Asio · TLS | 1024 | 4 | 4 / 4 | 317,590.56 | 199.25 | 257 | 174.92 | 18.02 | 3 |
| Boost.Asio · TCP | 1024 | 4 | 16 / 4 | 387,555.62 | 659.33 | 711.42 | 116.66 | 8.16 | 3 |
| Boost.Asio · TLS | 1024 | 4 | 16 / 4 | 337,043.25 | 757.58 | 835.63 | 136.47 | 15.53 | 3 |
| Standalone Asio · TCP | 1024 | 4 | 16 / 4 | 387,877.71 | 658.88 | 714.21 | 116.28 | 8.06 | 3 |
| Standalone Asio · TLS | 1024 | 4 | 16 / 4 | 336,694.35 | 758.13 | 832.58 | 136.57 | 15.41 | 3 |
| Boost.Asio · TCP | 1024 | 4 | 64 / 4 | 408,665.38 | 2,500.17 | 2,636.67 | 106.94 | 10.09 | 3 |
| Boost.Asio · TLS | 1024 | 4 | 64 / 4 | 351,318.71 | 2,904.54 | 3,255.29 | 121.68 | 18.58 | 3 |
| Standalone Asio · TCP | 1024 | 4 | 64 / 4 | 408,466.93 | 2,505.21 | 2,597.42 | 109.6 | 10.09 | 3 |
| Standalone Asio · TLS | 1024 | 4 | 64 / 4 | 350,430.29 | 2,900.58 | 3,423.13 | 122.38 | 18.3 | 3 |
| Boost.Asio · TCP | 4096 | 4 | 1 / 1 | 56,855.81 | 69.46 | 89.79 | 121.33 | 8.77 | 3 |
| Boost.Asio · TLS | 4096 | 4 | 1 / 1 | 49,630.35 | 79.71 | 104.54 | 127.43 | 16.25 | 3 |
| Standalone Asio · TCP | 4096 | 4 | 1 / 1 | 57,163.75 | 69.29 | 84.38 | 120.37 | 8.2 | 3 |
| Standalone Asio · TLS | 4096 | 4 | 1 / 1 | 49,944.62 | 79.17 | 102.96 | 127.5 | 15.41 | 3 |
| Boost.Asio · TCP | 4096 | 4 | 4 / 1 | 84,638.68 | 188.58 | 205.92 | 99.25 | 7.97 | 3 |
| Boost.Asio · TLS | 4096 | 4 | 4 / 1 | 72,581.01 | 219.38 | 268.67 | 117.54 | 15.48 | 3 |
| Standalone Asio · TCP | 4096 | 4 | 4 / 1 | 84,830 | 188.17 | 205.13 | 98.84 | 7.88 | 3 |
| Standalone Asio · TLS | 4096 | 4 | 4 / 1 | 72,283.5 | 220.13 | 271.75 | 118.15 | 15.31 | 3 |
| Boost.Asio · TCP | 4096 | 4 | 16 / 1 | 95,236.62 | 671.67 | 711.75 | 96.52 | 7.94 | 3 |
| Boost.Asio · TLS | 4096 | 4 | 16 / 1 | 86,307.63 | 739.13 | 815.54 | 112.8 | 15.25 | 3 |
| Standalone Asio · TCP | 4096 | 4 | 16 / 1 | 101,155.74 | 631.75 | 684.54 | 95.25 | 8.06 | 3 |
| Standalone Asio · TLS | 4096 | 4 | 16 / 1 | 85,858.47 | 740.5 | 843.04 | 113.62 | 15.8 | 3 |
| Boost.Asio · TCP | 4096 | 4 | 64 / 1 | 105,550.64 | 2,419.46 | 2,606.63 | 97.67 | 12.86 | 3 |
| Boost.Asio · TLS | 4096 | 4 | 64 / 1 | 91,753.63 | 2,787.42 | 2,905.08 | 110.6 | 18.66 | 3 |
| Standalone Asio · TCP | 4096 | 4 | 64 / 1 | 105,622.76 | 2,417.42 | 2,549.42 | 97.34 | 11.27 | 3 |
| Standalone Asio · TLS | 4096 | 4 | 64 / 1 | 91,802.2 | 2,787.58 | 2,900.13 | 110.01 | 19.91 | 3 |
| Boost.Asio · TCP | 4096 | 4 | 1 / 4 | 108,746.42 | 145.75 | 191.58 | 155.01 | 10.97 | 3 |
| Boost.Asio · TLS | 4096 | 4 | 1 / 4 | 83,859.05 | 189.38 | 243.75 | 165.54 | 16.7 | 3 |
| Standalone Asio · TCP | 4096 | 4 | 1 / 4 | 109,057.92 | 145.25 | 191.54 | 153.85 | 9.02 | 3 |
| Standalone Asio · TLS | 4096 | 4 | 1 / 4 | 84,103.59 | 189.17 | 240.33 | 165.32 | 18.11 | 3 |
| Boost.Asio · TCP | 4096 | 4 | 4 / 4 | 102,248.75 | 625.67 | 674.33 | 109.29 | 8.39 | 3 |
| Boost.Asio · TLS | 4096 | 4 | 4 / 4 | 86,659.14 | 735.54 | 827.21 | 129.59 | 16.42 | 3 |
| Standalone Asio · TCP | 4096 | 4 | 4 / 4 | 102,185.65 | 625.92 | 673.79 | 109.53 | 8.39 | 3 |
| Standalone Asio · TLS | 4096 | 4 | 4 / 4 | 86,922.74 | 734.79 | 824.21 | 129.43 | 16.31 | 3 |
| Boost.Asio · TCP | 4096 | 4 | 16 / 4 | 105,837.87 | 2,416.21 | 2,510.75 | 101.34 | 10.88 | 3 |
| Boost.Asio · TLS | 4096 | 4 | 16 / 4 | 89,467.66 | 2,857.17 | 2,999.83 | 115.42 | 18.11 | 3 |
| Standalone Asio · TCP | 4096 | 4 | 16 / 4 | 106,032.26 | 2,413.75 | 2,529.75 | 100.26 | 10.75 | 3 |
| Standalone Asio · TLS | 4096 | 4 | 16 / 4 | 89,640.31 | 2,851.71 | 2,990.58 | 114.95 | 17.91 | 3 |
| Boost.Asio · TCP | 4096 | 4 | 64 / 4 | 108,015.97 | 9,456.88 | 9,720.54 | 97.46 | 19.97 | 3 |
| Boost.Asio · TLS | 4096 | 4 | 64 / 4 | 93,400.65 | 10,944.08 | 11,445.79 | 111.42 | 30.7 | 3 |
| Standalone Asio · TCP | 4096 | 4 | 64 / 4 | 107,217.61 | 9,512.5 | 9,803.96 | 97.22 | 18.91 | 3 |
| Standalone Asio · TLS | 4096 | 4 | 64 / 4 | 93,627.03 | 10,923.21 | 11,133.29 | 111.4 | 26.95 | 3 |
| Boost.Asio · TCP | 16384 | 4 | 1 / 1 | 21,544.78 | 185.29 | 202.42 | 95.86 | 7.16 | 3 |
| Boost.Asio · TLS | 16384 | 4 | 1 / 1 | 17,514.6 | 226.38 | 281.25 | 108.69 | 15.2 | 3 |
| Standalone Asio · TCP | 16384 | 4 | 1 / 1 | 21,567.87 | 185.08 | 212.17 | 96.27 | 7.67 | 3 |
| Standalone Asio · TLS | 16384 | 4 | 1 / 1 | 17,583.3 | 226.04 | 269.33 | 107.96 | 15.16 | 3 |
| Boost.Asio · TCP | 16384 | 4 | 4 / 1 | 23,565.63 | 679.71 | 723.25 | 91 | 8.7 | 3 |
| Boost.Asio · TLS | 16384 | 4 | 4 / 1 | 21,246.52 | 751.29 | 823.75 | 110.95 | 15.7 | 3 |
| Standalone Asio · TCP | 16384 | 4 | 4 / 1 | 25,067.19 | 637.29 | 673.71 | 86.94 | 7.92 | 3 |
| Standalone Asio · TLS | 16384 | 4 | 4 / 1 | 21,327.6 | 748.17 | 827.04 | 110.02 | 15.13 | 3 |
| Boost.Asio · TCP | 16384 | 4 | 16 / 1 | 26,289.79 | 2,427.17 | 2,540.04 | 92.7 | 13.5 | 3 |
| Boost.Asio · TLS | 16384 | 4 | 16 / 1 | 23,069.59 | 2,770.88 | 2,907.63 | 108.53 | 19.39 | 3 |
| Standalone Asio · TCP | 16384 | 4 | 16 / 1 | 26,351.63 | 2,423.46 | 2,532.08 | 92.31 | 12.19 | 3 |
| Standalone Asio · TLS | 16384 | 4 | 16 / 1 | 22,893.83 | 2,778.58 | 3,057.79 | 109.65 | 20.5 | 3 |
| Boost.Asio · TCP | 16384 | 4 | 64 / 1 | 27,065.15 | 9,426.38 | 9,848.88 | 90.91 | 29.97 | 3 |
| Boost.Asio · TLS | 16384 | 4 | 64 / 1 | 23,457.73 | 10,850.38 | 11,419.21 | 107.53 | 35.44 | 3 |
| Standalone Asio · TCP | 16384 | 4 | 64 / 1 | 27,085.38 | 9,421.71 | 9,836.63 | 90.9 | 28.27 | 3 |
| Standalone Asio · TLS | 16384 | 4 | 64 / 1 | 23,532.65 | 10,806.71 | 11,560.58 | 107.24 | 43.42 | 3 |
| Boost.Asio · TCP | 16384 | 4 | 1 / 4 | 26,195.52 | 609.96 | 657.92 | 104.43 | 9.13 | 3 |
| Boost.Asio · TLS | 16384 | 4 | 1 / 4 | 20,977.21 | 760.88 | 841.75 | 123.29 | 16.56 | 3 |
| Standalone Asio · TCP | 16384 | 4 | 1 / 4 | 26,248.7 | 609.21 | 660.29 | 103.74 | 9.14 | 3 |
| Standalone Asio · TLS | 16384 | 4 | 1 / 4 | 20,959.05 | 761.71 | 832.58 | 123.77 | 16.31 | 3 |
| Boost.Asio · TCP | 16384 | 4 | 4 / 4 | 26,408.95 | 2,421.96 | 2,523.46 | 94.9 | 11.34 | 3 |
| Boost.Asio · TLS | 16384 | 4 | 4 / 4 | 21,839.04 | 2,885.13 | 4,472.71 | 114.43 | 18.92 | 3 |
| Standalone Asio · TCP | 16384 | 4 | 4 / 4 | 26,446 | 2,418.42 | 2,501.92 | 95.26 | 10.89 | 3 |
| Standalone Asio · TLS | 16384 | 4 | 4 / 4 | 22,249.38 | 2,873.42 | 3,039.04 | 111.31 | 18.16 | 3 |
| Boost.Asio · TCP | 16384 | 4 | 16 / 4 | 26,883.7 | 9,503.04 | 9,994.92 | 92.36 | 19.63 | 3 |
| Boost.Asio · TLS | 16384 | 4 | 16 / 4 | 23,337.51 | 10,935.21 | 11,917.17 | 109.01 | 36.03 | 3 |
| Standalone Asio · TCP | 16384 | 4 | 16 / 4 | 27,010.26 | 9,472.79 | 9,648.63 | 91.3 | 19.16 | 3 |
| Standalone Asio · TLS | 16384 | 4 | 16 / 4 | 23,428.05 | 10,922.79 | 11,118.79 | 108.68 | 34.22 | 3 |
| Boost.Asio · TCP | 16384 | 4 | 64 / 4 | 27,187.01 | 37,588.08 | 38,191.08 | 92.6 | 65.03 | 3 |
| Boost.Asio · TLS | 16384 | 4 | 64 / 4 | 24,174.63 | 41,582.88 | 44,782.42 | 107.18 | 77.11 | 3 |
| Standalone Asio · TCP | 16384 | 4 | 64 / 4 | 27,105.86 | 37,615.38 | 38,012.92 | 92.02 | 59.58 | 3 |
| Standalone Asio · TLS | 16384 | 4 | 64 / 4 | 23,582.93 | 41,974.21 | 45,786.46 | 106.27 | 71.88 | 3 |

</details>

## Reproduction and provenance

[Build and run the benchmark](../../../benchmarks/redis.md) · [API usage](usage.md)

[Download raw measurements and metadata](<https://github.com/OpenArkStudio/arknet/releases/download/v0.2.0/arknet-0.2.0-benchmark-data.tar.gz>)

Raw dataset SHA-256: `bce5d0926b3006f3c708208dc471301a961994d7064a2247db7340757de88453`

| Measured source | SHA-256 |
| --- | --- |
| benchmarks/redis.cpp | `9de41f49ae449a1f3be1b2015d9224340a7f87f5414be7ef3e50e2148ef6cdbc` |
| benchmarks/resources.hpp | `d6056569c5c660e6bc22adce7a3a583d87f8b6ec514770cc7dcf16f416fc8054` |
| include/arknet/base/detail/deadline.hpp | `767f8456a5961a6264e7c49aef4adc3438f63d0eaf5d15793a0d24c9224d4c0d` |
| include/arknet/base/io_pool.hpp | `8ee8e6eae6c715c65179101de849b95c2ce16873d8e361c1b279556cf2641dc1` |
| include/arknet/external/asio.hpp | `910980772d975b219e001d718af75e9a70d1a30ecd73fd38073051e78b339f8a` |
| include/arknet/redis/client.hpp | `952d403284598bbcaeece7373459190bb2773b7c60d46bb95eb66e8b49f1f849` |
| include/arknet/redis/detail/resp.hpp | `7c174f9d7cdaa0a30ec65cec858714d9ef82a25dcbd4d6a0297d0fd3541b8341` |
| include/arknet/redis/pool.hpp | `8cf76f81a872efb477861541580f57a0bb97cb2174cd77995308a6353ae63a47` |
