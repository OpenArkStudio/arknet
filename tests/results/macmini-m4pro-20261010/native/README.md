# Native TCP Validation

[English](README.md) | [中文](README_CN.md)

Validated on the Mac mini M4 Pro on 2026-10-10. These logs cover the native Asio
callback/coroutine benchmark harness after the batch flow-control fix.

| Configuration | Test cases | Assertions | Result |
| --- | ---: | ---: | --- |
| Standalone Asio 1.38.2, Release | 3 | 20 | Passed |
| Boost 1.90.0, Release | 3 | 20 | Passed |
| Standalone, Debug, ASan/UBSan | 3 | 20 | Passed |
| Standalone, Debug, TSan | 3 | 20 | Passed |

The permanent tests check cancellation after an injected failure, batch content
with windows 1/64, and the maximum payload of 65,507 bytes with 64 messages per
batch. Both execution modes and both multi-thread context topologies are tested.

`arknet-native-final-probes.log` records 136 successful checks on both backends
and execution modes:

- 96 checks: 64-byte messages, 4 clients, windows 1/4/16/64, and 0/10,000 CPU
  iterations per message, using one context/one thread, one context/four threads,
  and four contexts/four threads.
- 32 checks: 16 clients, 16,384-byte messages with window 64 or 64-byte messages
  with window 4, both CPU loads, and both four-thread context topologies.
- 8 checks: 65,507-byte messages, window 64, 4 clients, and both four-thread
  context topologies.

Each probe validates execution/backend metadata, content and IO error counts,
message count = batch observation count × window, RTT sample/percentile order,
positive warmup traffic, elapsed duration, and process CPU time. Window 1 reports
message RTT; larger windows report batch RTT. These are functional probes;
concurrent builds/tests may affect their timings. They are not formal
performance measurements.

Both servers read and echo full batches, applying the simulated CPU load to
each payload-sized message. This removes the opposing-write dependency
documented in the [pre-fix diagnostic](../../../../benchmarks/results/diagnostics/native-batch-flow-control/README.md).

The `*-build.log` and `*-test.log` files preserve all four build/test results.
Sanitizers used `halt_on_error=1`; no sanitizer error was reported.
Source SHA256 at validation:

```text
fe4b079850125820f88cc58c74f1bc63d3cdeab6415513f44475dc7d16eda565  benchmarks/coroutine.cpp
0158fdb3c9d6665a2c4d9fa4b03b75ec5ff26ae474a16222f19113bb70aa2713  tests/coroutine_shutdown.cpp
```
