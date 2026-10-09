[English](README.md) | [简体中文](README_CN.md)

# Portability Regression Checks

Date: 2026-10-10. Machine: Mac mini Mac16,11, Apple M4 Pro, 14 cores,
48 GiB RAM, macOS 27.0.1 (26A434). Dependencies: standalone Asio 1.38.2,
Boost 1.90.0 and OpenSSL 3.6.4. Clang builds use Apple Clang 21.0.0;
GCC builds use Homebrew GCC 15.2.0 with libstdc++.

| Configuration | Checks / cases | Result | Log |
| --- | ---: | --- | --- |
| Clang, standalone, Release, TLS | 41 / 133 | Passed, exit 0 | [Full check](arknet-format-standalone.log) |
| Clang, Boost, Release, TLS | 41 / 133 | Passed, exit 0 | [Full check](arknet-format-boost.log) |
| Clang, standalone, Debug, ASan/UBSan, TLS | 16 / 133 | Passed, exit 0 | [Full check](arknet-format-asan.log) |
| Clang, standalone, Debug, TSan, TLS | 16 / 133 | Passed, exit 0 | [Full check](arknet-format-tsan.log) |
| GCC, standalone, NoTLS, lifecycle | 1 / 18 | Passed, exit 0 | [Lifecycle](arknet-lifecycle-gcc-after.log) |
| GCC, standalone, NoTLS, TCP | 1 / 5 | Passed, exit 0 | [TCP](arknet-ci-fix-gcc-tcp.log) |
| GCC, standalone, NoTLS, detail | 1 / 11 | Passed, exit 0 | [Detail](arknet-ci-fix-gcc-detail.log) |
| GCC, standalone, NoTLS, IO pool | 1 / 6 | Passed, exit 0 | [IO pool](arknet-ci-fix-gcc-io_pool.log) |

The two Release builds include protocol tests, stress tests, an installed-package
consumer and 25 short benchmark checks. Sanitizer builds omit benchmarks and
use their default runtime options on this Mac. Neither sanitizer run reports
an error. Apple ASan does not provide a Linux LeakSanitizer result.

## Reproducing the Regression

With libstdc++, an unread future kept the user callable in a `std::packaged_task`
shared state after the canceled task was destroyed. The
[before log](arknet-lifecycle-gcc-before.log) records the failed callback-release
assertion. The fix separates the promise result state from the callable in
post, IO pool and queued-event tasks. Permanent lifecycle tests cover canceled
and completed captures, void/value/reference results, move-only captures and
results, and callback exceptions.

[GCC commands](arknet-ci-fix-gcc-commands.txt) record the exact compilation and
execution commands, versions and exit statuses. Clang checks ran with:

```sh
cmake --build /private/tmp/arknet-macmini-standalone --parallel 2 --target arknet_check
cmake --build /private/tmp/arknet-macmini-boost --parallel 2 --target arknet_check
cmake --build /private/tmp/arknet-macmini-asan --parallel 2 --target arknet_check
cmake --build /private/tmp/arknet-macmini-tsan --parallel 2 --target arknet_check
```

The MSVC matcher fix returns an explicit function pointer rather than deducing
a function reference through `decltype(auto)`. Existing TCP framing tests cover
this path. These local runs do not establish Windows or Linux runner results.

UDP send completions preserve the socket error and report zero bytes on failure.
A permanent simulated-stream test covers nonzero byte counts returned with
`message_size` and cancellation errors, successful sends, empty datagrams,
both connected and endpoint sends, and move-only callbacks. The real IPv4
65,507-byte send and 65,508-byte rejection tests remain in place. A zero-byte
error completion does not establish whether a canceled datagram reached its peer.

## Cancellation and Windows Regression

The [previous CI run](https://github.com/OpenArkStudio/arknet/actions/runs/38044247191)
found queued cancellation callbacks keeping their context alive through a shared
ownership cycle. The callback now uses a weak reference. A deterministic test
covers strand/direct executors and unrun/stopped contexts: all four contexts
remain owned before the fix, and all four are released after it on both backends.
The local full checks above include this regression. Linux LeakSanitizer also
passed on both backends in the cross-platform CI run below.

Windows also exceeded the 35-minute job limit. The UDP rejection test recorded
system error 1784 for the 65,508-byte send on both
[standalone](windows-udp-before-standalone.log) and [Boost](windows-udp-before-boost.log).
It now verifies failure, zero bytes and subsequent recovery, preserving the
native error. The simulated completion test includes error 1784. CI allows 60 minutes.

clang-format 18.1.8 and the formatting runner's self-check passed for all 126
project C++ files. A fresh install places notices in `share/arknet/docs/`.

## Cross-Platform CI

[CI run 38047445881](https://github.com/OpenArkStudio/arknet/actions/runs/38047445881)
passed all 13 jobs for source `6d8b7224bd6dbbe34154d112caad0f49952e2897`:

- Linux: standalone and Boost, Release with TLS on/off, ASan/UBSan with leak detection, and TSan.
- macOS and Windows: standalone and Boost, Release with TLS, including installed-package checks.
- Style: clang-format 18 and the formatting script's self-check.

The final documentation and search-index updates retain the same implementation,
tests and build configuration identified by the source checksums below.

[Source checksums](source.sha256) identify the current implementation, tests and style configuration.
[Earlier checksums](source-before-format.sha256) accompany the retained 132-case logs.
Previous performance measurements keep their original inputs in the provenance
archives; this check did not rerun the performance matrix. IPv6 validation
remains limited to loopback as described in the [IPv6 record](../ipv6/README.md).
