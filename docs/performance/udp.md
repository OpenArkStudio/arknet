# UDP Local Performance

Measured on 2026-10-12: all 180 measurements passed. [Machine, source and raw records](udp-update.md).

Measurements come from independent local Release loopback processes, not CI.

[Metric definitions and methodology](methodology.md).

Average process CPU uses 100% for one logical CPU and can exceed 100% with multiple threads; estimated values are labeled, and incomplete sets of three CPU samples show N/A. On this 14-logical-CPU host, machine share = process CPU% / 14; 100% of one CPU is 7.14% of the machine.

## No added computation

![Throughput](assets/udp-work0-throughput.svg)

![p99 RTT](assets/udp-work0-latency.svg)

### All Measured Profiles

#### 64 B / 1 clients / window=1

| Dependency / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 47,450 [45,394, 48,080] | 5.79 | 20.00 | 25.42 | 29.58 | 76.5 | 7.31 |
| Boost.Asio / 4 contexts / 4 threads | 47,571 [47,358, 48,884] | 5.81 | 20.46 | 25.04 | 28.04 | 76.6 | 7.38 |
| Boost.Asio / 1 context / 1 thread | 47,800 [47,377, 48,173] | 5.83 | 19.96 | 24.96 | 27.71 | 76.5 | 7.27 |
| Boost.Asio / 1 context / 2 threads | 39,196 [39,116, 39,504] | 4.78 | 25.29 | 27.96 | 30.92 | 107.5 | 7.28 |
| Boost.Asio / 1 context / 4 threads | 36,336 [36,285, 36,459] | 4.44 | 26.92 | 30.75 | 35.25 | 135.6 | 7.34 |
| Standalone Asio / 2 contexts / 2 threads | 48,210 [48,209, 49,779] | 5.88 | 19.67 | 24.88 | 27.58 | 76.4 | 7.77 |
| Standalone Asio / 4 contexts / 4 threads | 48,184 [47,948, 49,389] | 5.88 | 19.79 | 24.88 | 27.08 | 76.7 | 7.83 |
| Standalone Asio / 1 context / 1 thread | 48,329 [47,812, 48,758] | 5.90 | 19.62 | 25.21 | 28.46 | 76.8 | 7.72 |
| Standalone Asio / 1 context / 2 threads | 38,969 [38,177, 39,165] | 4.76 | 25.33 | 28.29 | 32.21 | 107.6 | 7.73 |
| Standalone Asio / 1 context / 4 threads | 36,067 [34,595, 36,300] | 4.40 | 26.83 | 31.83 | 40.88 | 135.0 | 7.83 |

#### 1024 B / 1 clients / window=1

| Dependency / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 46,495 [46,240, 47,976] | 90.81 | 19.75 | 25.46 | 28.96 | 77.0 | 7.33 |
| Boost.Asio / 4 contexts / 4 threads | 46,915 [45,246, 47,353] | 91.63 | 19.71 | 25.04 | 27.92 | 77.1 | 7.36 |
| Boost.Asio / 1 context / 1 thread | 46,845 [46,003, 48,688] | 91.49 | 19.83 | 25.21 | 27.88 | 77.5 | 7.27 |
| Boost.Asio / 1 context / 2 threads | 37,768 [37,708, 38,135] | 73.77 | 25.29 | 28.33 | 31.42 | 108.6 | 7.33 |
| Boost.Asio / 1 context / 4 threads | 33,090 [32,871, 34,097] | 64.63 | 28.29 | 35.25 | 50.33 | 131.2 | 7.36 |
| Standalone Asio / 2 contexts / 2 threads | 47,119 [46,674, 47,533] | 92.03 | 19.71 | 24.83 | 27.46 | 77.2 | 7.81 |
| Standalone Asio / 4 contexts / 4 threads | 46,156 [45,644, 47,506] | 90.15 | 19.83 | 26.00 | 32.50 | 77.1 | 7.83 |
| Standalone Asio / 1 context / 1 thread | 48,807 [47,522, 48,885] | 95.33 | 19.46 | 23.54 | 26.88 | 76.1 | 7.75 |
| Standalone Asio / 1 context / 2 threads | 37,537 [37,305, 38,166] | 73.31 | 25.54 | 28.33 | 31.04 | 108.6 | 7.77 |
| Standalone Asio / 1 context / 4 threads | 34,351 [34,196, 34,388] | 67.09 | 28.04 | 31.21 | 34.29 | 129.3 | 7.83 |

#### 16384 B / 1 clients / window=1

| Dependency / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 30,251 [29,613, 30,545] | 945.33 | 22.00 | 27.21 | 32.46 | 85.2 | 7.38 |
| Boost.Asio / 4 contexts / 4 threads | 30,532 [30,109, 30,547] | 954.11 | 21.67 | 26.83 | 31.25 | 85.4 | 7.44 |
| Boost.Asio / 1 context / 1 thread | 30,478 [30,387, 30,678] | 952.45 | 21.54 | 26.96 | 31.62 | 85.5 | 7.34 |
| Boost.Asio / 1 context / 2 threads | 28,199 [28,114, 28,384] | 881.23 | 24.46 | 30.12 | 34.50 | 112.2 | 7.38 |
| Boost.Asio / 1 context / 4 threads | 26,114 [24,852, 26,615] | 816.05 | 26.83 | 33.04 | 38.33 | 115.3 | 7.44 |
| Standalone Asio / 2 contexts / 2 threads | 30,982 [30,282, 31,051] | 968.19 | 21.38 | 26.79 | 30.92 | 85.4 | 7.86 |
| Standalone Asio / 4 contexts / 4 threads | 30,636 [30,420, 31,031] | 957.37 | 21.50 | 26.88 | 30.38 | 85.4 | 7.91 |
| Standalone Asio / 1 context / 1 thread | 30,759 [30,621, 31,213] | 961.22 | 21.29 | 26.75 | 30.79 | 85.5 | 7.86 |
| Standalone Asio / 1 context / 2 threads | 28,358 [28,328, 28,788] | 886.20 | 24.79 | 30.17 | 33.67 | 112.6 | 7.91 |
| Standalone Asio / 1 context / 4 threads | 26,103 [25,864, 26,946] | 815.71 | 26.67 | 32.71 | 37.33 | 115.5 | 7.92 |

#### 64 B / 16 clients / window=1

| Dependency / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 89,861 [89,688, 89,979] | 10.97 | 175.92 | 209.04 | 225.33 | 129.6 | 9.70 |
| Boost.Asio / 4 contexts / 4 threads | 80,403 [79,950, 80,423] | 9.81 | 198.88 | 236.38 | 253.58 | 143.7 | 9.80 |
| Boost.Asio / 1 context / 1 thread | 96,708 [96,210, 97,603] | 11.81 | 163.71 | 178.04 | 195.33 | 99.8 | 9.67 |
| Boost.Asio / 1 context / 2 threads | 98,170 [96,859, 98,526] | 11.98 | 165.21 | 237.21 | 264.83 | 156.0 | 9.66 |
| Boost.Asio / 1 context / 4 threads | 80,010 [76,919, 80,099] | 9.77 | 197.12 | 249.67 | 293.83 | 250.2 | 9.72 |
| Standalone Asio / 2 contexts / 2 threads | 90,329 [89,825, 90,937] | 11.03 | 174.75 | 207.79 | 223.88 | 129.4 | 10.17 |
| Standalone Asio / 4 contexts / 4 threads | 80,669 [80,462, 81,223] | 9.85 | 197.88 | 235.58 | 253.29 | 143.2 | 10.27 |
| Standalone Asio / 1 context / 1 thread | 99,324 [98,087, 99,993] | 12.12 | 158.71 | 177.21 | 190.25 | 99.9 | 10.12 |
| Standalone Asio / 1 context / 2 threads | 92,289 [92,210, 95,205] | 11.27 | 172.33 | 182.58 | 193.54 | 170.4 | 10.17 |
| Standalone Asio / 1 context / 4 threads | 80,783 [80,345, 81,159] | 9.86 | 195.17 | 230.83 | 279.04 | 262.1 | 10.20 |

#### 1024 B / 16 clients / window=1

| Dependency / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 80,295 [78,801, 80,889] | 156.83 | 196.42 | 224.96 | 241.46 | 124.3 | 9.75 |
| Boost.Asio / 4 contexts / 4 threads | 75,977 [75,132, 76,649] | 148.39 | 209.21 | 239.42 | 254.42 | 145.5 | 9.86 |
| Boost.Asio / 1 context / 1 thread | 90,452 [89,186, 90,467] | 176.66 | 175.25 | 194.21 | 207.96 | 99.7 | 9.67 |
| Boost.Asio / 1 context / 2 threads | 95,095 [88,323, 95,632] | 185.73 | 170.46 | 241.54 | 270.71 | 157.4 | 9.69 |
| Boost.Asio / 1 context / 4 threads | 78,477 [78,309, 79,461] | 153.28 | 199.50 | 253.04 | 305.71 | 253.0 | 9.80 |
| Standalone Asio / 2 contexts / 2 threads | 80,872 [78,417, 81,971] | 157.95 | 197.50 | 222.83 | 235.54 | 124.1 | 10.22 |
| Standalone Asio / 4 contexts / 4 threads | 76,327 [75,496, 76,450] | 149.08 | 208.25 | 240.33 | 255.92 | 143.4 | 10.38 |
| Standalone Asio / 1 context / 1 thread | 92,515 [92,463, 92,687] | 180.69 | 167.38 | 190.04 | 207.79 | 99.4 | 10.19 |
| Standalone Asio / 1 context / 2 threads | 86,611 [84,684, 87,475] | 169.16 | 181.96 | 199.25 | 211.58 | 173.3 | 10.19 |
| Standalone Asio / 1 context / 4 threads | 79,047 [78,744, 79,979] | 154.39 | 197.29 | 238.08 | 294.88 | 265.7 | 10.30 |

#### 16384 B / 16 clients / window=1

| Dependency / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 49,359 [48,426, 49,404] | 1,542.46 | 313.12 | 335.83 | 355.08 | 138.0 | 10.41 |
| Boost.Asio / 4 contexts / 4 threads | 55,783 [55,409, 56,063] | 1,743.21 | 274.21 | 313.04 | 346.62 | 178.1 | 10.52 |
| Boost.Asio / 1 context / 1 thread | 39,387 [38,996, 39,458] | 1,230.85 | 395.67 | 414.62 | 435.54 | 99.6 | 10.44 |
| Boost.Asio / 1 context / 2 threads | 63,013 [60,733, 65,185] | 1,969.16 | 243.50 | 310.17 | 374.42 | 185.2 | 10.47 |
| Boost.Asio / 1 context / 4 threads | 69,132 [68,934, 69,797] | 2,160.38 | 215.62 | 264.25 | 312.33 | 295.8 | 10.48 |
| Standalone Asio / 2 contexts / 2 threads | 48,938 [48,735, 48,992] | 1,529.31 | 315.62 | 335.71 | 348.96 | 139.2 | 10.91 |
| Standalone Asio / 4 contexts / 4 threads | 55,537 [55,485, 55,923] | 1,735.54 | 274.46 | 306.83 | 326.54 | 178.2 | 11.00 |
| Standalone Asio / 1 context / 1 thread | 40,600 [40,560, 41,176] | 1,268.76 | 382.54 | 403.38 | 424.83 | 100.0 | 10.89 |
| Standalone Asio / 1 context / 2 threads | 60,677 [60,419, 60,720] | 1,896.14 | 255.75 | 264.62 | 276.17 | 188.8 | 10.92 |
| Standalone Asio / 1 context / 4 threads | 71,873 [71,163, 72,138] | 2,246.03 | 207.00 | 246.79 | 307.04 | 309.8 | 10.97 |

Tables show repeated-run medians; throughput brackets show min/max, not confidence intervals. Latencies are medians of per-run sampled percentiles, without pooling samples.
