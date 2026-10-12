# RUDP, ARQ and HTTP Request Validation

2026-10-12, Mac mini `Mac16,11`, Apple M4 Pro, 14 cores (10 performance and
4 efficiency), 48 GiB RAM, macOS 27.0.1 (26A434), Apple Clang 21.0.0,
CMake 4.3.3, Standalone Asio 1.38.2, Boost 1.90.0 and OpenSSL 3.6.4.
Source: `e1cdce5e58aa3f7d0376e8ccfda21448650c17a7` plus the changes in the
commit containing this record. These additions are not part of `v0.1.0-rc.1`.

## Results

| Check | Cases / assertions | Evidence |
| --- | ---: | --- |
| Standalone Asio, Release + TLS, complete suite | 48 checks / 174 cases | [Log](arknet-rudp-final-standalone.log) |
| Boost.Asio, Release + TLS, complete suite | 48 checks / 174 cases | [Log](arknet-rudp-final-boost.log) |
| ARQ core, ASan/UBSan | 19 / 17,824 | [Log](arknet-arq-final-asan.log) |
| Canonical KCP interop, ASan/UBSan | 3 / 21,571 | [Log](arknet-arq-interop-final-asan.log) |
| RUDP network, ASan/UBSan | 10 / 452 | [Log](arknet-rudp-final-asan.log) |
| HTTP requests, ASan/UBSan | 8 / 70 | [Log](arknet-httprequest-final-asan.log) |
| RUDP network, TSan | 10 / 452 | [Log](arknet-rudp-tsan.log) |
| HTTP requests, TSan | 8 / 70 | [Log](arknet-rudp-http-tsan.log) |

All checks passed. Complete suites include the installed consumer and three
RUDP benchmark smoke configurations. Their timings are not performance results.
TSan checks target the new network modules; this record does not represent a
fresh run of the entire historical TSan suite.

The complete documentation programs for the RUDP server/client, single HTTP
request and matching HTTP server were also compiled and run with both backends.
Both pairs returned the expected echo and stopped normally. The English and
Chinese pages contain identical C++ source blocks.

ARQ tests cover ordered reassembly, loss/reordering, finite retries, timestamp
wraparound, malformed datagrams, bounded memory and interoperability with the
test-only canonical implementation. RUDP tests cover handshake retries,
fragmented messages, IPv4/IPv6, cancellation, shutdown notifications, stale SYN
rejection and receive-budget exhaustion. HTTP request tests use independent
raw peers for HEAD, interim/chunked/EOF responses, limits, TRACE validation,
cancellation and exactly-once completion.

## Reproduction

Build and run the complete suite:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
cmake --build build --target arknet_check
```

For Boost.Asio, replace the Asio include argument with
`-DARKNET_USE_BOOST_ASIO=ON`. Individual C++ tests run directly:

```sh
build/tests/arknet_arq_core_test
build/tests/arknet_arq_interop_test
build/tests/arknet_rudp_test
build/tests/arknet_httprequest_test
```

For a separate sanitizer build, set `-DARKNET_BUILD_BENCHMARKS=OFF`,
`-DCMAKE_BUILD_TYPE=Debug` and
`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`.
Run with `ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`.
For TSan, use another build with `-fsanitize=thread -fno-omit-frame-pointer`
and run with `TSAN_OPTIONS=halt_on_error=1`.

Only `tests/vendor/kcp/ikcp.c` disables the `null,object-size` sanitizer checks:
the reference uses null-member offset macros. The arknet implementation keeps
both checks enabled. Apple ASan passing is not a Linux LeakSanitizer result.

## Scope

These local tests do not establish WAN capacity, long-duration production
stability, authentication or FEC behavior. The separate
[RUDP performance report](../../../../docs/performance/rudp.md) retains measured
results, machine metadata and the permanent C++ benchmark.
