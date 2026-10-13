# HTTP deadline validation

2026-10-13, Mac mini `Mac16,11`, Apple M4 Pro, 14 cores, 48 GiB RAM,
macOS 27.0.1, Apple Clang 21.0.0, CMake 4.3.3, Standalone Asio 1.38.2,
Boost 1.90.0 and OpenSSL 3.6.4. Source: `e767cdf` plus the HTTP deadline
fix and tests in the commit containing this record.

| Build | Result |
| --- | --- |
| Standalone Asio, Release + TLS | [10 cases / 283 assertions passed](httprequest-standalone.log) |
| Boost.Asio, Release + TLS | [10 cases / 283 assertions passed](httprequest-boost.log) |
| Standalone Asio, Debug + ASan/UBSan | [10 cases / 283 assertions passed](httprequest-asan-ubsan.log) |
| Standalone Asio, Debug + TSan | [10 cases / 283 assertions passed](httprequest-tsan.log) |

The published `e767cdf` header fails the new queued-response regression:
[control result](published-header-regression.log). A ready IO handler can run
before an expired timer's handler. Requests now check the absolute deadline
before handling DNS, connect, write and read results. Coverage includes successful
and truncated responses queued while IO is paused, exactly-once completion,
and 64 short connect deadlines across numeric and DNS hosts.

Run the permanent C++ test after building:

```sh
build/tests/arknet_httprequest_test
```

The macOS ASan run uses `detect_leaks=0`; it does not establish leak-check results.
These functional and sanitizer checks are not performance measurements. The
RUDP benchmark manifest remains pinned to `5ba514e`, whose 960 measured source
files were verified against the recorded SHA-256 manifest.
