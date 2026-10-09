# HTTPS Local Performance

Measurements come from independent local Release loopback processes, not CI.

[Metric definitions and methodology](../testing.md).

Average process CPU uses 100% for one logical CPU and can exceed 100% with multiple threads; legacy values are marked estimated, and incomplete sets of three CPU samples show N/A. On this 14-logical-CPU host, machine share = process CPU% / 14; 100% of one CPU is 7.14% of the machine.

## No added computation

![Throughput](assets/https-work0-throughput.svg)

![p99 RTT](assets/https-work0-latency.svg)

### All Measured Profiles

#### 64 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 20,294 [20,113, 20,316] | 2.48 | 47.88 | 53.92 | 58.96 | 53.7 (estimated) | 14.25 |
| Boost.Asio / 4 contexts / 4 threads | 20,356 [20,315, 20,390] | 2.48 | 47.88 | 53.75 | 59.58 | 53.5 (estimated) | 14.36 |
| Boost.Asio / 1 context / 1 thread | 19,858 [19,554, 19,901] | 2.42 | 49.17 | 56.12 | 63.58 | 45.8 (estimated) | 14.22 |
| Boost.Asio / 1 context / 2 threads | 30,134 [29,840, 30,171] | 3.68 | 32.96 | 36.88 | 41.92 | 90.2 (estimated) | 14.27 |
| Boost.Asio / 1 context / 4 threads | 25,721 [24,572, 26,175] | 3.14 | 37.96 | 44.96 | 58.21 | 144.6 (estimated) | 14.41 |
| Standalone Asio / 2 contexts / 2 threads | 20,331 [20,112, 20,402] | 2.48 | 47.88 | 54.38 | 61.46 | 53.4 (estimated) | 14.53 |
| Standalone Asio / 4 contexts / 4 threads | 20,451 [20,405, 20,462] | 2.50 | 47.88 | 53.62 | 58.71 | 53.2 (estimated) | 14.59 |
| Standalone Asio / 1 context / 1 thread | 19,934 [19,878, 20,000] | 2.43 | 49.17 | 55.12 | 61.75 | 45.7 (estimated) | 14.50 |
| Standalone Asio / 1 context / 2 threads | 30,231 [29,865, 30,398] | 3.69 | 33.00 | 37.00 | 41.12 | 89.7 (estimated) | 14.47 |
| Standalone Asio / 1 context / 4 threads | 26,312 [25,915, 26,496] | 3.21 | 37.25 | 43.79 | 48.62 | 144.5 (estimated) | 14.64 |

#### 1024 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 13,002 [13,001, 13,031] | 25.39 | 75.33 | 83.33 | 90.75 | 41.1 (estimated) | 14.22 |
| Boost.Asio / 4 contexts / 4 threads | 12,966 [12,957, 12,977] | 25.32 | 75.38 | 83.38 | 89.88 | 41.2 (estimated) | 14.41 |
| Boost.Asio / 1 context / 1 thread | 12,861 [12,852, 12,867] | 25.12 | 75.38 | 84.04 | 90.88 | 36.1 (estimated) | 14.27 |
| Boost.Asio / 1 context / 2 threads | 27,126 [27,074, 27,234] | 52.98 | 35.75 | 40.58 | 44.17 | 92.2 (estimated) | 14.23 |
| Boost.Asio / 1 context / 4 threads | 22,481 [22,271, 22,671] | 43.91 | 43.29 | 47.38 | 53.25 | 132.9 (estimated) | 14.38 |
| Standalone Asio / 2 contexts / 2 threads | 12,942 [12,935, 12,972] | 25.28 | 75.33 | 83.71 | 90.79 | 41.0 (estimated) | 14.41 |
| Standalone Asio / 4 contexts / 4 threads | 12,940 [12,915, 13,003] | 25.27 | 75.38 | 84.00 | 90.83 | 41.4 (estimated) | 14.64 |
| Standalone Asio / 1 context / 1 thread | 12,851 [12,840, 12,872] | 25.10 | 75.38 | 84.25 | 90.67 | 36.1 (estimated) | 14.44 |
| Standalone Asio / 1 context / 2 threads | 27,256 [26,993, 27,431] | 53.23 | 35.25 | 40.58 | 44.38 | 92.5 (estimated) | 14.62 |
| Standalone Asio / 1 context / 4 threads | 22,813 [22,478, 22,890] | 44.56 | 43.04 | 47.08 | 53.33 | 133.5 (estimated) | 14.61 |

#### 16384 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 2,051 [2,042, 2,065] | 64.08 | 467.83 | 524.92 | 558.67 | 27.9 (estimated) | 14.19 |
| Boost.Asio / 4 contexts / 4 threads | 2,036 [2,010, 2,056] | 63.61 | 470.08 | 533.00 | 578.04 | 28.3 (estimated) | 14.31 |
| Boost.Asio / 1 context / 1 thread | 2,079 [2,063, 2,079] | 64.98 | 460.00 | 530.67 | 551.21 | 25.9 (estimated) | 14.08 |
| Boost.Asio / 1 context / 2 threads | 13,900 [13,809, 13,908] | 434.39 | 62.50 | 67.54 | 72.79 | 112.6 (estimated) | 14.38 |
| Boost.Asio / 1 context / 4 threads | 12,741 [12,644, 12,763] | 398.14 | 67.79 | 76.21 | 83.58 | 126.0 (estimated) | 14.45 |
| Standalone Asio / 2 contexts / 2 threads | 2,037 [2,022, 2,049] | 63.66 | 470.50 | 537.92 | 564.58 | 28.4 (estimated) | 14.55 |
| Standalone Asio / 4 contexts / 4 threads | 2,040 [2,035, 2,047] | 63.77 | 470.29 | 535.21 | 562.83 | 28.3 (estimated) | 14.55 |
| Standalone Asio / 1 context / 1 thread | 2,051 [2,017, 2,062] | 64.10 | 463.83 | 540.67 | 569.38 | 26.6 (estimated) | 14.44 |
| Standalone Asio / 1 context / 2 threads | 14,051 [13,966, 14,194] | 439.08 | 61.58 | 67.33 | 74.42 | 112.7 (estimated) | 14.66 |
| Standalone Asio / 1 context / 4 threads | 12,785 [12,688, 12,819] | 399.54 | 67.29 | 76.29 | 83.33 | 126.1 (estimated) | 14.66 |

#### 64 B / 16 clients / window=16

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 175,116 [173,850, 176,065] | 21.38 | 1,433.96 | 1,571.38 | 2,420.17 | 198.9 (estimated) | 34.88 |
| Boost.Asio / 4 contexts / 4 threads | 242,978 [239,507, 243,970] | 29.66 | 1,007.33 | 1,337.83 | 1,426.38 | 395.6 (estimated) | 35.33 |
| Boost.Asio / 1 context / 1 thread | 107,049 [105,963, 108,285] | 13.07 | 2,373.75 | 2,459.62 | 2,509.62 | 99.6 (estimated) | 34.69 |
| Boost.Asio / 1 context / 2 threads | 152,177 [151,802, 154,215] | 18.58 | 1,659.71 | 1,766.46 | 1,947.75 | 196.0 (estimated) | 34.83 |
| Boost.Asio / 1 context / 4 threads | 184,173 [182,491, 188,316] | 22.48 | 1,352.79 | 1,555.92 | 1,800.50 | 374.9 (estimated) | 35.05 |
| Standalone Asio / 2 contexts / 2 threads | 179,694 [175,930, 182,835] | 21.94 | 1,382.67 | 1,610.00 | 2,586.92 | 199.6 (estimated) | 35.19 |
| Standalone Asio / 4 contexts / 4 threads | 249,736 [248,220, 250,321] | 30.49 | 971.46 | 1,336.50 | 1,373.88 | 395.8 (estimated) | 35.66 |
| Standalone Asio / 1 context / 1 thread | 109,102 [108,352, 110,028] | 13.32 | 2,328.83 | 2,418.62 | 2,498.12 | 99.6 (estimated) | 35.08 |
| Standalone Asio / 1 context / 2 threads | 156,113 [154,563, 157,233] | 19.06 | 1,624.21 | 1,766.88 | 2,050.00 | 197.1 (estimated) | 35.12 |
| Standalone Asio / 1 context / 4 threads | 187,737 [178,601, 189,684] | 22.92 | 1,323.42 | 1,601.04 | 1,854.12 | 375.9 (estimated) | 35.56 |

#### 1024 B / 16 clients / window=16

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 137,989 [136,530, 142,428] | 269.51 | 1,827.12 | 1,961.12 | 2,211.96 | 196.5 (estimated) | 36.19 |
| Boost.Asio / 4 contexts / 4 threads | 200,526 [199,385, 203,190] | 391.65 | 1,241.00 | 1,513.29 | 1,704.33 | 378.0 (estimated) | 36.66 |
| Boost.Asio / 1 context / 1 thread | 70,758 [70,632, 71,113] | 138.20 | 3,606.08 | 3,697.50 | 3,838.04 | 91.5 (estimated) | 36.02 |
| Boost.Asio / 1 context / 2 threads | 120,762 [119,913, 121,133] | 235.86 | 2,105.79 | 2,192.29 | 2,494.25 | 196.3 (estimated) | 36.36 |
| Boost.Asio / 1 context / 4 threads | 148,840 [143,913, 151,042] | 290.70 | 1,680.92 | 1,983.29 | 2,245.42 | 377.2 (estimated) | 36.34 |
| Standalone Asio / 2 contexts / 2 threads | 139,721 [134,804, 142,361] | 272.89 | 1,814.17 | 1,935.79 | 2,035.75 | 195.5 (estimated) | 36.30 |
| Standalone Asio / 4 contexts / 4 threads | 201,031 [199,232, 201,977] | 392.64 | 1,245.04 | 1,520.38 | 1,705.38 | 373.9 (estimated) | 36.86 |
| Standalone Asio / 1 context / 1 thread | 76,411 [72,148, 77,229] | 149.24 | 3,359.33 | 3,457.71 | 3,561.71 | 97.0 (estimated) | 36.22 |
| Standalone Asio / 1 context / 2 threads | 122,440 [121,199, 123,559] | 239.14 | 2,077.42 | 2,155.46 | 2,282.58 | 197.5 (estimated) | 36.27 |
| Standalone Asio / 1 context / 4 threads | 153,770 [152,089, 154,198] | 300.33 | 1,639.12 | 1,826.88 | 2,188.08 | 380.9 (estimated) | 36.34 |

#### 16384 B / 16 clients / window=16

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 27,748 [27,630, 27,897] | 867.13 | 9,199.00 | 9,406.08 | 9,819.88 | 165.1 (estimated) | 47.66 |
| Boost.Asio / 4 contexts / 4 threads | 36,513 [36,076, 36,613] | 1,141.02 | 6,970.50 | 7,404.04 | 7,572.17 | 286.2 (estimated) | 47.73 |
| Boost.Asio / 1 context / 1 thread | 13,860 [13,754, 13,929] | 433.14 | 18,386.62 | 18,619.67 | 18,986.46 | 86.7 (estimated) | 47.38 |
| Boost.Asio / 1 context / 2 threads | 30,848 [29,985, 30,864] | 963.99 | 8,289.00 | 8,526.25 | 8,841.92 | 197.7 (estimated) | 47.66 |
| Boost.Asio / 1 context / 4 threads | 39,174 [39,129, 39,241] | 1,224.18 | 6,505.17 | 6,759.83 | 7,173.88 | 380.9 (estimated) | 47.77 |
| Standalone Asio / 2 contexts / 2 threads | 27,761 [27,646, 27,908] | 867.55 | 9,191.33 | 9,430.83 | 9,895.75 | 162.6 (estimated) | 47.73 |
| Standalone Asio / 4 contexts / 4 threads | 36,285 [36,266, 37,201] | 1,133.90 | 6,999.88 | 7,325.46 | 7,613.58 | 288.5 (estimated) | 47.92 |
| Standalone Asio / 1 context / 1 thread | 14,047 [13,997, 14,083] | 438.97 | 18,163.17 | 18,661.54 | 18,827.46 | 86.7 (estimated) | 47.72 |
| Standalone Asio / 1 context / 2 threads | 31,183 [31,128, 31,232] | 974.46 | 8,195.12 | 8,399.96 | 8,680.50 | 197.7 (estimated) | 47.69 |
| Standalone Asio / 1 context / 4 threads | 38,219 [38,210, 40,229] | 1,194.35 | 6,418.29 | 7,187.62 | 9,227.62 | 380.7 (estimated) | 47.83 |

Tables show repeated-run medians; throughput brackets show min/max, not confidence intervals. Latencies are medians of per-run sampled percentiles, without pooling samples.
