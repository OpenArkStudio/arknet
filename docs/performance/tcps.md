# TCP+TLS Local Performance

Measurements come from independent local Release loopback processes, not CI.

[Metric definitions and methodology](../testing.md).

Average process CPU uses 100% for one logical CPU and can exceed 100% with multiple threads; legacy values are marked estimated, and incomplete sets of three CPU samples show N/A. On this 14-logical-CPU host, machine share = process CPU% / 14; 100% of one CPU is 7.14% of the machine.

## No added computation

![Throughput](assets/tcps-work0-throughput.svg)

![p99 RTT](assets/tcps-work0-latency.svg)

### All Measured Profiles

#### 64 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 51,540 [50,864, 51,618] | 6.29 | 18.83 | 23.54 | 27.58 | 88.4 (estimated) | 13.48 |
| Boost.Asio / 4 contexts / 4 threads | 51,150 [50,499, 51,535] | 6.24 | 18.92 | 23.83 | 28.25 | 88.8 (estimated) | 13.61 |
| Boost.Asio / 1 context / 1 thread | 49,767 [49,522, 50,002] | 6.08 | 20.50 | 23.96 | 27.75 | 83.2 (estimated) | 13.34 |
| Boost.Asio / 1 context / 2 threads | 46,177 [44,566, 46,698] | 5.64 | 21.08 | 25.79 | 32.21 | 116.3 (estimated) | 13.56 |
| Boost.Asio / 1 context / 4 threads | 39,558 [38,518, 39,641] | 4.83 | 24.67 | 29.88 | 35.17 | 152.3 (estimated) | 13.48 |
| Standalone Asio / 2 contexts / 2 threads | 46,297 [46,221, 50,170] | 5.65 | 20.29 | 26.75 | 37.75 | 90.6 (estimated) | 13.88 |
| Standalone Asio / 4 contexts / 4 threads | 47,580 [46,594, 51,310] | 5.81 | 20.12 | 26.04 | 31.79 | 90.2 (estimated) | 13.97 |
| Standalone Asio / 1 context / 1 thread | 48,332 [47,075, 49,726] | 5.90 | 20.46 | 24.96 | 29.38 | 81.9 (estimated) | 13.80 |
| Standalone Asio / 1 context / 2 threads | 43,561 [42,611, 45,615] | 5.32 | 22.75 | 26.92 | 31.83 | 117.5 (estimated) | 13.86 |
| Standalone Asio / 1 context / 4 threads | 37,406 [37,152, 38,608] | 4.57 | 25.17 | 35.29 | 54.33 | 149.1 (estimated) | 13.89 |

#### 1024 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 46,758 [44,557, 47,324] | 91.32 | 20.00 | 25.50 | 29.21 | 94.0 (estimated) | 13.58 |
| Boost.Asio / 4 contexts / 4 threads | 46,710 [44,717, 46,936] | 91.23 | 20.04 | 25.42 | 29.29 | 94.0 (estimated) | 13.53 |
| Boost.Asio / 1 context / 1 thread | 47,572 [47,236, 47,724] | 92.91 | 20.08 | 24.62 | 28.29 | 86.2 (estimated) | 13.48 |
| Boost.Asio / 1 context / 2 threads | 43,103 [42,811, 43,395] | 84.19 | 22.12 | 26.71 | 32.00 | 120.9 (estimated) | 13.50 |
| Boost.Asio / 1 context / 4 threads | 36,724 [36,587, 36,965] | 71.73 | 25.83 | 30.83 | 37.04 | 137.7 (estimated) | 13.59 |
| Standalone Asio / 2 contexts / 2 threads | 43,676 [43,206, 46,163] | 85.31 | 21.42 | 27.12 | 32.79 | 96.8 (estimated) | 13.94 |
| Standalone Asio / 4 contexts / 4 threads | 44,001 [41,953, 45,610] | 85.94 | 21.00 | 27.42 | 33.79 | 95.4 (estimated) | 13.95 |
| Standalone Asio / 1 context / 1 thread | 47,247 [46,643, 47,259] | 92.28 | 20.38 | 25.12 | 29.67 | 85.4 (estimated) | 13.84 |
| Standalone Asio / 1 context / 2 threads | 40,759 [40,652, 41,739] | 79.61 | 23.38 | 27.92 | 32.88 | 121.7 (estimated) | 13.83 |
| Standalone Asio / 1 context / 4 threads | 35,933 [34,939, 35,966] | 70.18 | 26.33 | 32.58 | 44.83 | 137.5 (estimated) | 13.94 |

#### 16384 B / 1 clients / window=1

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 12,852 [12,311, 12,917] | 401.63 | 64.42 | 77.08 | 99.25 | 89.6 (estimated) | 13.66 |
| Boost.Asio / 4 contexts / 4 threads | 12,671 [12,503, 12,696] | 395.97 | 65.21 | 79.83 | 105.50 | 89.8 (estimated) | 13.69 |
| Boost.Asio / 1 context / 1 thread | 12,447 [12,345, 12,548] | 388.97 | 70.33 | 79.46 | 88.17 | 67.7 (estimated) | 13.64 |
| Boost.Asio / 1 context / 2 threads | 17,709 [17,707, 17,942] | 553.42 | 44.92 | 52.42 | 70.12 | 121.9 (estimated) | 13.70 |
| Boost.Asio / 1 context / 4 threads | 17,121 [17,050, 17,865] | 535.03 | 46.38 | 56.25 | 77.62 | 129.0 (estimated) | 13.72 |
| Standalone Asio / 2 contexts / 2 threads | 12,901 [12,046, 12,955] | 403.16 | 65.25 | 75.29 | 81.50 | 89.6 (estimated) | 14.06 |
| Standalone Asio / 4 contexts / 4 threads | 12,801 [12,792, 12,835] | 400.05 | 65.04 | 77.00 | 88.96 | 89.4 (estimated) | 14.06 |
| Standalone Asio / 1 context / 1 thread | 12,426 [12,330, 13,034] | 388.31 | 69.21 | 79.83 | 89.17 | 67.3 (estimated) | 13.98 |
| Standalone Asio / 1 context / 2 threads | 17,193 [17,033, 17,516] | 537.30 | 45.50 | 57.42 | 107.75 | 121.0 (estimated) | 14.03 |
| Standalone Asio / 1 context / 4 threads | 17,102 [15,336, 17,141] | 534.42 | 47.00 | 55.00 | 77.79 | 128.7 (estimated) | 14.14 |

#### 64 B / 16 clients / window=16

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 235,367 [196,640, 245,898] | 28.73 | 1,018.79 | 1,519.50 | 1,998.17 | 198.4 (estimated) | 33.94 |
| Boost.Asio / 4 contexts / 4 threads | 295,973 [288,969, 296,431] | 36.13 | 877.04 | 1,184.17 | 1,242.50 | 392.6 (estimated) | 34.47 |
| Boost.Asio / 1 context / 1 thread | 130,701 [128,800, 135,530] | 15.95 | 1,963.08 | 2,056.96 | 2,249.08 | 98.6 (estimated) | 33.75 |
| Boost.Asio / 1 context / 2 threads | 191,203 [188,374, 196,004] | 23.34 | 1,317.83 | 1,491.96 | 1,626.58 | 193.2 (estimated) | 33.94 |
| Boost.Asio / 1 context / 4 threads | 234,844 [231,059, 246,525] | 28.67 | 1,058.29 | 1,447.46 | 1,674.04 | 370.8 (estimated) | 34.12 |
| Standalone Asio / 2 contexts / 2 threads | 247,980 [231,956, 250,996] | 30.27 | 985.17 | 1,310.92 | 1,797.50 | 199.1 (estimated) | 34.41 |
| Standalone Asio / 4 contexts / 4 threads | 302,330 [268,790, 315,770] | 36.91 | 836.04 | 1,160.88 | 1,232.54 | 392.6 (estimated) | 34.77 |
| Standalone Asio / 1 context / 1 thread | 131,202 [129,019, 145,120] | 16.02 | 1,936.08 | 2,059.71 | 2,158.42 | 97.0 (estimated) | 34.12 |
| Standalone Asio / 1 context / 2 threads | 184,158 [180,633, 198,705] | 22.48 | 1,301.88 | 1,471.75 | 1,759.92 | 195.9 (estimated) | 34.28 |
| Standalone Asio / 1 context / 4 threads | 247,099 [233,439, 254,342] | 30.16 | 1,033.75 | 1,295.92 | 1,493.46 | 372.9 (estimated) | 34.52 |

#### 1024 B / 16 clients / window=16

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 188,519 [185,430, 201,912] | 368.20 | 1,246.83 | 2,019.71 | 2,401.83 | 198.5 (estimated) | 34.81 |
| Boost.Asio / 4 contexts / 4 threads | 259,126 [251,591, 278,153] | 506.11 | 996.62 | 1,240.38 | 1,305.29 | 394.7 (estimated) | 35.53 |
| Boost.Asio / 1 context / 1 thread | 102,232 [100,722, 103,783] | 199.67 | 2,471.08 | 2,641.67 | 3,407.38 | 95.8 (estimated) | 34.58 |
| Boost.Asio / 1 context / 2 threads | 169,689 [166,315, 170,448] | 331.42 | 1,505.50 | 1,710.42 | 1,916.29 | 195.5 (estimated) | 34.81 |
| Boost.Asio / 1 context / 4 threads | 230,046 [215,719, 230,924] | 449.31 | 1,115.12 | 1,332.04 | 1,529.29 | 378.6 (estimated) | 35.16 |
| Standalone Asio / 2 contexts / 2 threads | 188,619 [181,678, 197,707] | 368.40 | 1,249.08 | 2,073.33 | 2,346.62 | 198.4 (estimated) | 35.06 |
| Standalone Asio / 4 contexts / 4 threads | 248,090 [247,535, 267,255] | 484.55 | 1,035.12 | 1,257.46 | 1,325.46 | 394.6 (estimated) | 35.42 |
| Standalone Asio / 1 context / 1 thread | 105,254 [104,778, 108,683] | 205.57 | 2,416.46 | 2,495.38 | 2,657.79 | 96.1 (estimated) | 34.88 |
| Standalone Asio / 1 context / 2 threads | 168,696 [161,784, 171,526] | 329.48 | 1,504.12 | 1,802.08 | 2,020.21 | 196.8 (estimated) | 34.94 |
| Standalone Asio / 1 context / 4 threads | 233,780 [230,894, 235,048] | 456.60 | 1,095.21 | 1,325.62 | 1,549.92 | 380.0 (estimated) | 35.58 |

#### 16384 B / 16 clients / window=16

| Backend / model | Round trips/s | MiB/s | p50 / us | p95 / us | p99 / us | CPU / % | RSS / MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Boost.Asio / 2 contexts / 2 threads | 48,596 [41,893, 49,622] | 1,518.62 | 5,139.58 | 6,142.54 | 6,740.75 | 199.4 (estimated) | 47.42 |
| Boost.Asio / 4 contexts / 4 threads | 81,774 [78,667, 82,297] | 2,555.43 | 3,086.67 | 3,442.96 | 3,578.46 | 397.4 (estimated) | 71.70 |
| Boost.Asio / 1 context / 1 thread | 24,233 [24,111, 24,497] | 757.28 | 10,453.83 | 11,265.50 | 12,300.83 | 98.3 (estimated) | 47.03 |
| Boost.Asio / 1 context / 2 threads | 47,868 [45,187, 48,153] | 1,495.89 | 5,346.04 | 5,952.96 | 6,364.79 | 198.7 (estimated) | 47.39 |
| Boost.Asio / 1 context / 4 threads | 74,060 [68,925, 74,330] | 2,314.37 | 3,460.42 | 4,189.96 | 4,492.17 | 390.7 (estimated) | 47.39 |
| Standalone Asio / 2 contexts / 2 threads | 44,825 [41,184, 48,053] | 1,400.77 | 5,604.33 | 6,659.12 | 6,968.25 | 198.5 (estimated) | 47.61 |
| Standalone Asio / 4 contexts / 4 threads | 79,503 [78,050, 82,746] | 2,484.46 | 3,229.62 | 3,519.17 | 3,603.33 | 398.1 (estimated) | 47.66 |
| Standalone Asio / 1 context / 1 thread | 24,356 [23,595, 25,355] | 761.12 | 10,243.71 | 11,459.33 | 11,601.12 | 98.3 (estimated) | 47.62 |
| Standalone Asio / 1 context / 2 threads | 43,203 [42,987, 47,018] | 1,350.10 | 5,830.12 | 6,996.71 | 7,441.54 | 197.8 (estimated) | 47.47 |
| Standalone Asio / 1 context / 4 threads | 72,029 [66,510, 72,980] | 2,250.90 | 3,535.29 | 4,623.58 | 5,210.58 | 389.4 (estimated) | 47.64 |

Tables show repeated-run medians; throughput brackets show min/max, not confidence intervals. Latencies are medians of per-run sampled percentiles, without pooling samples.
