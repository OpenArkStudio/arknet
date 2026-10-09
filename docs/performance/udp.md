# UDP Local Performance

Measurements come from independent local Release loopback processes, not CI.

[Metric definitions and methodology](../testing.md).

Average process CPU uses 100% for one logical CPU and can exceed 100% with multiple threads; legacy values are marked estimated, and incomplete sets of three CPU samples show N/A. On this 14-logical-CPU host, machine share = process CPU% / 14; 100% of one CPU is 7.14% of the machine.

## No added computation

![Throughput](assets/udp-work0-throughput.svg)

![p99 RTT](assets/udp-work0-latency.svg)

### All Measured Profiles

#### 64 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 47,660 [47,151, 48,301] | 5.82 | 19.58 | 25.62 | 30.50 | 76.3 (estimated) | 7.33 |
| Boost.Asio / 4 contexts / 4 threads | 46,875 [46,780, 47,429] | 5.72 | 20.00 | 25.46 | 30.54 | 76.7 (estimated) | 7.41 |
| Boost.Asio / 1 context / 1 thread | 47,127 [47,105, 47,822] | 5.75 | 19.67 | 25.71 | 30.54 | 76.4 (estimated) | 7.27 |
| Boost.Asio / 1 context / 2 threads | 39,474 [38,607, 40,529] | 4.82 | 24.92 | 28.58 | 35.17 | 107.5 (estimated) | 7.31 |
| Boost.Asio / 1 context / 4 threads | 35,442 [35,173, 35,933] | 4.33 | 27.04 | 34.00 | 48.21 | 136.8 (estimated) | 7.38 |
| Standalone Asio / 2 contexts / 2 threads | 48,064 [47,970, 49,975] | 5.87 | 19.33 | 25.46 | 29.58 | 76.2 (estimated) | 7.81 |
| Standalone Asio / 4 contexts / 4 threads | 47,409 [47,204, 49,732] | 5.79 | 19.50 | 25.33 | 30.42 | 76.2 (estimated) | 7.84 |
| Standalone Asio / 1 context / 1 thread | 47,941 [47,647, 48,485] | 5.85 | 19.38 | 25.12 | 27.71 | 76.2 (estimated) | 7.80 |
| Standalone Asio / 1 context / 2 threads | 40,636 [39,288, 40,989] | 4.96 | 24.25 | 27.29 | 30.88 | 108.1 (estimated) | 7.81 |
| Standalone Asio / 1 context / 4 threads | 35,237 [35,083, 36,088] | 4.30 | 27.33 | 32.75 | 47.71 | 136.0 (estimated) | 7.89 |

#### 1024 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 45,837 [45,522, 46,086] | 89.53 | 19.62 | 25.50 | 30.71 | 77.1 (estimated) | 7.36 |
| Boost.Asio / 4 contexts / 4 threads | 45,923 [45,306, 45,953] | 89.69 | 19.75 | 25.75 | 30.92 | 77.2 (estimated) | 7.41 |
| Boost.Asio / 1 context / 1 thread | 46,278 [45,860, 47,470] | 90.39 | 19.62 | 25.67 | 30.38 | 77.2 (estimated) | 7.28 |
| Boost.Asio / 1 context / 2 threads | 38,616 [38,335, 39,266] | 75.42 | 24.46 | 28.71 | 35.42 | 109.6 (estimated) | 7.33 |
| Boost.Asio / 1 context / 4 threads | 33,905 [33,583, 34,487] | 66.22 | 28.08 | 32.88 | 46.12 | 130.9 (estimated) | 7.39 |
| Standalone Asio / 2 contexts / 2 threads | 46,492 [46,232, 46,537] | 90.80 | 19.54 | 26.08 | 32.79 | 76.1 (estimated) | 7.81 |
| Standalone Asio / 4 contexts / 4 threads | 45,201 [44,828, 46,215] | 88.28 | 19.62 | 26.92 | 35.21 | 77.0 (estimated) | 7.88 |
| Standalone Asio / 1 context / 1 thread | 46,476 [45,988, 46,514] | 90.77 | 19.62 | 25.79 | 30.33 | 77.2 (estimated) | 7.78 |
| Standalone Asio / 1 context / 2 threads | 38,694 [38,471, 39,119] | 75.57 | 24.25 | 28.54 | 35.62 | 109.0 (estimated) | 7.83 |
| Standalone Asio / 1 context / 4 threads | 34,205 [34,068, 34,790] | 66.81 | 28.04 | 31.50 | 36.04 | 129.8 (estimated) | 7.92 |

#### 16384 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 29,694 [29,461, 29,782] | 927.94 | 22.92 | 28.71 | 35.62 | 85.1 (estimated) | 7.39 |
| Boost.Asio / 4 contexts / 4 threads | 29,097 [29,075, 29,268] | 909.29 | 23.33 | 29.21 | 37.92 | 84.4 (estimated) | 7.45 |
| Boost.Asio / 1 context / 1 thread | 29,376 [29,288, 29,679] | 918.01 | 23.25 | 28.92 | 35.21 | 85.2 (estimated) | 7.36 |
| Boost.Asio / 1 context / 2 threads | 26,052 [25,889, 26,485] | 814.12 | 27.50 | 32.96 | 42.58 | 112.0 (estimated) | 7.42 |
| Boost.Asio / 1 context / 4 threads | 25,086 [24,595, 25,248] | 783.95 | 28.04 | 33.54 | 41.58 | 116.0 (estimated) | 7.47 |
| Standalone Asio / 2 contexts / 2 threads | 29,711 [29,625, 30,008] | 928.48 | 23.04 | 27.33 | 32.25 | 85.1 (estimated) | 7.88 |
| Standalone Asio / 4 contexts / 4 threads | 29,711 [28,790, 29,797] | 928.47 | 23.00 | 27.29 | 31.79 | 85.1 (estimated) | 7.97 |
| Standalone Asio / 1 context / 1 thread | 29,554 [28,785, 30,048] | 923.57 | 23.08 | 27.71 | 32.83 | 85.1 (estimated) | 7.84 |
| Standalone Asio / 1 context / 2 threads | 26,709 [25,870, 26,835] | 834.65 | 26.88 | 30.46 | 35.21 | 112.3 (estimated) | 7.88 |
| Standalone Asio / 1 context / 4 threads | 24,564 [23,938, 24,790] | 767.63 | 28.54 | 34.46 | 46.79 | 114.9 (estimated) | 7.95 |

#### 64 B / 16 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 94,485 [92,299, 94,934] | 11.53 | 166.08 | 203.83 | 229.25 | 127.9 (estimated) | 9.72 |
| Boost.Asio / 4 contexts / 4 threads | 84,041 [83,737, 84,443] | 10.26 | 187.88 | 233.71 | 276.04 | 143.3 (estimated) | 9.81 |
| Boost.Asio / 1 context / 1 thread | 92,673 [92,477, 93,497] | 11.31 | 169.71 | 192.42 | 223.75 | 99.4 (estimated) | 9.62 |
| Boost.Asio / 1 context / 2 threads | 103,904 [99,098, 109,163] | 12.68 | 153.75 | 228.62 | 271.50 | 154.9 (estimated) | 9.72 |
| Boost.Asio / 1 context / 4 threads | 77,515 [77,277, 81,220] | 9.46 | 200.79 | 265.96 | 311.96 | 248.6 (estimated) | 9.77 |
| Standalone Asio / 2 contexts / 2 threads | 96,482 [95,232, 97,207] | 11.78 | 162.71 | 197.62 | 212.92 | 129.1 (estimated) | 10.17 |
| Standalone Asio / 4 contexts / 4 threads | 79,947 [77,098, 83,831] | 9.76 | 194.00 | 254.17 | 340.75 | 143.5 (estimated) | 10.30 |
| Standalone Asio / 1 context / 1 thread | 93,533 [92,396, 94,398] | 11.42 | 169.29 | 180.29 | 194.17 | 99.4 (estimated) | 10.12 |
| Standalone Asio / 1 context / 2 threads | 95,407 [94,747, 95,907] | 11.65 | 167.71 | 178.62 | 209.75 | 169.5 (estimated) | 10.19 |
| Standalone Asio / 1 context / 4 threads | 80,757 [77,302, 81,002] | 9.86 | 195.46 | 230.21 | 273.46 | 260.6 (estimated) | 10.23 |

#### 1024 B / 16 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 82,444 [82,027, 83,093] | 161.02 | 190.46 | 227.12 | 267.83 | 123.3 (estimated) | 9.73 |
| Boost.Asio / 4 contexts / 4 threads | 78,902 [78,093, 80,276] | 154.11 | 198.21 | 238.88 | 287.92 | 144.0 (estimated) | 9.86 |
| Boost.Asio / 1 context / 1 thread | 84,496 [81,589, 86,343] | 165.03 | 182.12 | 218.33 | 313.00 | 99.0 (estimated) | 9.75 |
| Boost.Asio / 1 context / 2 threads | 101,745 [100,597, 102,060] | 198.72 | 157.46 | 228.88 | 260.25 | 157.0 (estimated) | 9.70 |
| Boost.Asio / 1 context / 4 threads | 76,916 [75,977, 78,635] | 150.23 | 201.38 | 270.75 | 323.83 | 249.7 (estimated) | 9.80 |
| Standalone Asio / 2 contexts / 2 threads | 83,046 [81,137, 83,916] | 162.20 | 188.46 | 224.33 | 260.58 | 123.5 (estimated) | 10.22 |
| Standalone Asio / 4 contexts / 4 threads | 77,486 [75,960, 80,113] | 151.34 | 202.67 | 246.79 | 300.21 | 144.3 (estimated) | 10.33 |
| Standalone Asio / 1 context / 1 thread | 87,835 [87,224, 88,466] | 171.55 | 178.29 | 197.12 | 221.50 | 99.5 (estimated) | 10.23 |
| Standalone Asio / 1 context / 2 threads | 92,811 [92,163, 93,108] | 181.27 | 173.75 | 187.00 | 210.92 | 171.8 (estimated) | 10.23 |
| Standalone Asio / 1 context / 4 threads | 75,743 [75,332, 76,862] | 147.93 | 202.21 | 268.79 | 310.46 | 261.8 (estimated) | 10.27 |

#### 16384 B / 16 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 51,123 [50,793, 51,359] | 1,597.59 | 300.79 | 337.92 | 378.58 | 137.4 (estimated) | 10.42 |
| Boost.Asio / 4 contexts / 4 threads | 55,418 [53,914, 56,458] | 1,731.82 | 271.42 | 328.00 | 414.75 | 175.5 (estimated) | 10.52 |
| Boost.Asio / 1 context / 1 thread | 38,733 [38,668, 38,914] | 1,210.40 | 397.79 | 435.25 | 472.33 | 99.3 (estimated) | 10.39 |
| Boost.Asio / 1 context / 2 threads | 67,725 [67,485, 68,771] | 2,116.42 | 216.08 | 314.17 | 366.12 | 185.5 (estimated) | 10.45 |
| Boost.Asio / 1 context / 4 threads | 67,407 [55,701, 69,308] | 2,106.47 | 219.58 | 294.17 | 351.75 | 295.1 (estimated) | 10.45 |
| Standalone Asio / 2 contexts / 2 threads | 50,988 [50,288, 51,493] | 1,593.37 | 301.50 | 328.12 | 350.21 | 135.6 (estimated) | 10.95 |
| Standalone Asio / 4 contexts / 4 threads | 55,211 [54,867, 56,788] | 1,725.34 | 268.54 | 342.54 | 440.17 | 174.9 (estimated) | 11.05 |
| Standalone Asio / 1 context / 1 thread | 38,878 [38,617, 38,892] | 1,214.93 | 396.58 | 436.29 | 503.38 | 99.5 (estimated) | 10.89 |
| Standalone Asio / 1 context / 2 threads | 71,894 [71,129, 72,004] | 2,246.70 | 212.00 | 238.79 | 314.29 | 191.8 (estimated) | 10.95 |
| Standalone Asio / 1 context / 4 threads | 71,513 [59,482, 71,972] | 2,234.77 | 207.12 | 252.79 | 311.50 | 308.7 (estimated) | 11.03 |

Tables show repeated-run medians; throughput brackets show min/max, not confidence intervals. Latencies are medians of per-run sampled percentiles, without pooling samples.
