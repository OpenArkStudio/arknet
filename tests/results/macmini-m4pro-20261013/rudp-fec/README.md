# RUDP and FEC Validation

[English](README.md) | [中文](README_CN.md)

2026-10-13, Mac mini `Mac16,11`, Apple M4 Pro, 14 cores (10 performance and
4 efficiency), 48 GiB RAM, macOS 27.0.1 (26A434), Apple Clang 21.0.0,
CMake 4.3.3, Standalone Asio 1.38.2, Boost 1.90.0 and OpenSSL 3.6.4.
Source: published base `15ff563` plus the changes in the commit containing this
record. The measured source manifest and binary hashes are retained with the
[performance data](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results/macmini-m4pro-20261013/rudp-fec/metadata.json).
RUDP/FEC is not included in `v0.1.0-rc.1`.

## Results

| Check | Coverage | Result |
| --- | --- | --- |
| Standalone Asio, Release + TLS | 55 checks / 219 doctest cases | [Passed](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec/arknet-rudp-fec-check-standalone.log) |
| Boost.Asio, Release + TLS | 55 checks / 219 doctest cases | [Passed](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec/arknet-rudp-fec-check-boost.log) |
| ASan/UBSan, Standalone Asio | 10 selected suites / 109 cases | [Logs](arknet-rudp-final-asan/) |
| TSan, Standalone Asio | 8 selected suites / 86 cases | [Logs](arknet-rudp-final-tsan/) |
| Real zephyr/arknet peers, each dependency configuration | 12 process pairs / 16 sessions / 512 messages and echoes | [Evidence](interop/arknet-zephyr-interop-final.md) |
| Formatting and exclusion rules | 146 project C++ files; third-party exclusions | [Check](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec/arknet-rudp-fec-format.log), [runner](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec/arknet-rudp-fec-format-runner.log) |
| Report generator | Schema/accounting checks, linked SVGs, XML and rendering | [Log](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec/arknet-rudp-report-verification.log) |
| Bilingual documentation | Build, routes, local references and AI Markdown exports | [Log](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/rudp-fec/arknet-rudp-fec-docs-check.log) |

Complete checks include installed-package consumption and benchmark smoke cases;
smoke timings are not published performance results. RUDP FEC passed 11 cases
and 2,564 assertions with both Release dependency configurations and both sanitizer builds.
ASan/UBSan covers ARQ, reference ARQ/FEC interop, RUDP/FEC, lifecycle, detail and
concurrency. TSan covers FEC, reference FEC interop, RUDP/FEC, lifecycle, detail
and concurrency. This record does not claim a complete local sanitizer run of
every historical suite.

The network measurements completed 96 configurations / 276 measured runs and
7,960,439 message round trips without delivery, ordering, completion, drain or
invalid-FEC errors. They include six 120-second sustained runs. Performance and
sanitizer executions are separate. See the [RUDP report](../../../../docs/performance/rudp.md).

## Reproduction

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --target arknet_check --parallel 3
build/tests/arknet_fec_test
build/tests/arknet_fec_interop_test
build/tests/arknet_rudp_stability_test
build/tests/arknet_rudp_fec_test
```

For Boost.Asio, replace the Asio path with `-DARKNET_USE_BOOST_ASIO=ON` and use
a separate build directory. Optional real-peer tests need the legacy checkout:

```sh
cmake -S . -B build-interop -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include \
  -DARKNET_ZEPHYR_ROOT=/path/to/zephyr
cmake --build build-interop --target arknet_zephyr_interop_check --parallel 3
```

The [interop instructions](../../../interop/README.md) describe the scenarios
and generated, build-only legacy compatibility overlays. The legacy checkout
is not modified or installed. These tests use ARQ-only sessions and preserve
SYN/ACK/FIN; they do not establish reliable shutdown if FIN is lost.

For separate sanitizer builds, disable benchmarks and use Debug. ASan/UBSan:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DARKNET_BUILD_BENCHMARKS=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include \
  '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build-asan --parallel 3
ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
  build-asan/tests/arknet_rudp_fec_test
```

This macOS run uses `detect_leaks=0`; it is not a Linux leak-check result.
Only the test-only canonical KCP source disables `null,object-size` checks;
arknet's own code remains fully instrumented. For TSan, use another build with
`-fsanitize=thread -fno-omit-frame-pointer` in compiler and executable linker
flags, then run with `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1`.
The FEC endpoint suite took 18.67 seconds under TSan; kernel UDP receive-buffer
drop counters did not increase. Its bounded load uses 1 MiB effective socket
buffers and an ARQ send window of 16, while retaining concurrent sends/statistics.

## Adoption Scope

These tests establish the exercised library behavior and real legacy-peer
interoperability. ARK business integration, cross-host deployment, descriptor
exhaustion, production-duration endurance and deployment authentication remain
separate acceptance tasks. See [production acceptance](../../../../docs/project/production-readiness.md).
