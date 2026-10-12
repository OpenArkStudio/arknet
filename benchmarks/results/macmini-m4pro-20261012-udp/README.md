# UDP Retest: 2026-10-12

Mac mini Mac16,11, Apple M4 Pro (14 cores), 48 GiB, macOS 27.0.1, Apple Clang 21. Release builds with standalone Asio 1.38.2 and Boost 1.90.0.

| Files | Measurements | Pass / fail |
| --- | ---: | ---: |
| `*-io.json` | 180 | 180 / 0 |
| `*-dimensions.json` | 216 | 186 / 30 |
| `limits/*.json` | 108 | 72 / 36 |
| `buffer-probes/*.json` | 21 | 9 / 12 |
| `*-stress.log` | 60 seconds per backend | Both passed |

Each matrix JSON records settings, commands, host, source/executable hashes and failures. All six matrix files use source SHA-256 `0771574af8ca56f1914deb544b956079e0a2f24021953ded5f5d524e3df334b1`.

Isolation runs instrument the server receive count and allow `--udp-server-receive-buffer`. `buffer-probes/benchmark-instrumentation.patch` records that change; `commands.sh` retains the executed shell script, and `executable-sha256.txt` identifies the binary. Kernel snapshots are taken before and after each process. The 4 MiB / 16 KiB / window-64 probe loses 767 messages, with exactly 767 additional kernel full-buffer drops and no IPv4 fragmentation/reassembly.

[Report](../../../docs/performance/udp-update.md) · [中文报告](../../../docs/performance/udp-update_CN.md) · [Buffer configuration](../../../docs/guides/socket-buffers.md)
