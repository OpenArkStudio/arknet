[English](README.md) | [简体中文](README_CN.md)

# IPv6 loopback checks

Validated on 2026-10-10: macOS 27.0.1 arm64, Apple Clang 21.0.0, Release C++20,
Standalone Asio 1.38.2, Boost 1.90.0, OpenSSL 3.6.4.

| Configuration | Cases | Assertions | Result |
| --- | ---: | ---: | --- |
| Standalone, TLS on | 11 | 114 | Passed |
| Boost, TLS on | 11 | 114 | Passed |
| Standalone, TLS off | 7 | 69 | Passed |
| Boost, TLS off | 7 | 69 | Passed |
| Existing WebSocket suite, standalone | 3 | 17 | Passed |
| Existing WebSocket suite, Boost | 3 | 17 | Passed |
| Standalone, ASan/UBSan, TLS on | 11 | 114 | Passed |
| Standalone, TSan, TLS on | 11 | 114 | Passed |

[Permanent doctest cases](../../../ipv6.cpp) cover TCP, UDP sessions, UDP cast,
WebSocket, TLS, WSS, HTTP and HTTPS over `::1`, including binary payloads,
empty UDP datagrams, resolver and peer addresses, HTTP routing, bracketed
Host fields with the actual port, TLS IP identity verification, absent DNS
SNI for a literal IP, and rejection of an IPv4-only certificate.

The pre-fix log records three failed Host assertions: WS over IPv6, WS over
IPv4 with a nondefault port, and WSS over IPv6. The fix formats the handshake
authority as `[::1]:port` or `127.0.0.1:port`. The IPv4 check remains permanent.
The UDP address accessor also compiles with current Asio and clears a stale
error after successful address conversion.

Run from the repository root with the dependencies installed:

```sh
cmake -S . -B build-ipv6 -DARKNET_BUILD_TESTS=ON -DARKNET_ENABLE_SSL=ON -DARKNET_USE_BOOST_ASIO=OFF
cmake --build build-ipv6 --target arknet_ipv6_test
./build-ipv6/tests/arknet_ipv6_test
```

Set `ARKNET_USE_BOOST_ASIO=ON` for Boost; set `ARKNET_ENABLE_SSL=OFF` for the
seven non-TLS cases. The TLS cases use the separate [IPv6 test certificates](../../../certs/ipv6),
whose `generate.sh` regenerates its self-signed test identity and explicit trust anchor.
Existing measurement certificates are unchanged.

`asan.log` retains an initial startup failure with `detect_leaks=1`, which macOS
does not support. The subsequent run passed with `detect_leaks=0:halt_on_error=1`
and `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. The TSan run in `tsan.log`
used `TSAN_OPTIONS=halt_on_error=1` and reported no races. Apple ASan did not
check for leaks in this run.

## Benchmark entry checks

Each backend's `*-protocol-probes.json` contains seven probes: TCP, UDP,
WebSocket, TLS, WSS, HTTP and HTTPS. Each `*-native-probes.json` contains eight
probes: callback/coroutine, one context/four threads or four contexts/one thread
each, and zero or 10,000 computation iterations. All 30 probes passed.
Each uses 0.2 seconds of measurement and 0.1 seconds of warmup, validating IPv6
options and CPU percentage output rather than performance conclusions. JSON
records include commands, host details and source hashes.

CPU percentage was checked against `CPU time / cpu_elapsed_seconds * 100`,
allowing less than 0.001 percentage points of output rounding error.
Every `address_family` is `ipv6`; TLS uses the separate IPv6 identity.
Both backends' `arknet_ipv6_echo` examples were built and ran successfully.

These checks cover local IPv6 loopback only. They do not validate external
IPv6 routing, scoped link-local addresses, DNS family fallback, or dual-stack
wildcard listeners. The OS default for `v6_only` is unchanged.
