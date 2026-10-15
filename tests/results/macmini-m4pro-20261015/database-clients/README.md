# Database Client Validation

Validation date: 2026-10-15. This record covers the source containing this file,
whose CMake version metadata is `0.2.0`. Validation does not publish a release.

## Environment

| Item | Configuration |
| --- | --- |
| Machine | Mac mini `Mac16,11`, Apple M4 Pro, 14 cores: 10 performance and 4 efficiency |
| Memory / OS | 48 GiB / macOS 27.0.1 (26A434) |
| Release compiler | Apple Clang 21.0.0 |
| Sanitizer compiler | Homebrew LLVM 22.1.7, arm64-apple-darwin27.0.0 |
| Build / framework | CMake 4.3.3 / doctest 2.5.3 |
| Dependencies | Boost 1.90.0, Boost.Asio 1.38.0, Standalone Asio 1.38.2, OpenSSL 3.6.4 |
| Servers | Official MySQL Community 8.4.11 ARM64 and Redis 7.4.2, loopback only |

## Results

Complete Release checks use the integration source, TLS enabled, and real
database cases enabled. These totals cover the complete `arknet_check` registry,
including protocol/codecs, process checks, installed consumers and benchmark
smoke cases. They are not performance measurements.

| Dependency | Checks | doctest cases | Assertions | Failed / skipped |
| --- | ---: | ---: | ---: | --- |
| Boost.Asio | 75 | 374 | 1,952,026 | 0 / 0 |
| Standalone Asio | 62 | 314 | 976,980 | 0 / 0 |

HTTP and WebSocket require Boost.Beast and account for different protocol
coverage; Standalone Asio also has its own thread-storage configuration checks.
Checks count invoked programs; cases and assertions count doctest output only.
Process-exit/global-owner/multi-translation-unit and listener-stop checks remain
separate from the database targets below.

| Instrumentation / dependency | Target | Cases | Assertions | Failed / skipped |
| --- | --- | ---: | ---: | --- |
| TSan / Boost.Asio | MySQL connection | 34 | 1,488 | 0 / 0 |
| TSan / Boost.Asio | MySQL pool | 17 | 411 | 0 / 0 |
| TSan / Standalone Asio | MySQL connection | 34 | 1,488 | 0 / 0 |
| TSan / Standalone Asio | MySQL pool | 17 | 411 | 0 / 0 |
| ASan+UBSan / Boost.Asio | MySQL connection | 34 | 1,488 | 0 / 0 |
| ASan+UBSan / Boost.Asio | MySQL pool | 17 | 411 | 0 / 0 |
| TSan / Boost.Asio | Redis client | 21 | 183 | 0 / 0 |
| TSan / Boost.Asio | Redis pool | 14 | 133 | 0 / 0 |
| ASan+UBSan / Standalone Asio | Redis client | 21 | 183 | 0 / 0 |
| ASan+UBSan / Standalone Asio | Redis pool | 14 | 133 | 0 / 0 |

The four Boost.Asio TSan targets total **86 cases / 2,215 assertions**.
Standalone Asio MySQL TSan and Boost.Asio MySQL ASan+UBSan each pass
**51 cases / 1,899 assertions**. Standalone Asio Redis ASan+UBSan passes
**35 cases / 316 assertions**. MySQL and Redis were executed separately against
the recorded product headers, not as one simultaneous service-load run.
No sanitizer diagnostics were reported, and no suppressions were used.
macOS ASan runs disabled leak detection; they do not establish Linux
LeakSanitizer coverage.

## Scope

| Area | Validated behavior |
| --- | --- |
| MySQL connection | Verified TLS and wrong identity/password rejection; greeting/authentication fragmentation and sequence validation; native and caching-SHA2 authentication; query, prepared/binary results, transactions, result limits and exact physical-packet continuation |
| MySQL cancellation / pool | Pre-post count/byte admission, deadlines including queue time, sent-request unknown outcomes without replay, FIFO exclusive leases, callback drain, real reset, configured database restoration and stale-statement rejection |
| MySQL posting lifetime | Runtime ownership until asynchronous posting returns, command timers released before runtime owners, completion-driven destruction for successful, cancelled, invalid-connect and rejected-command paths |
| Redis client | RESP2/RESP3 authentication, real TCP/TLS/mTLS, binary pipelines, reply ordering, synchronous admission, cancellation tombstones retaining reply slots/budgets and bounded reply drain |
| Redis pool | Exclusive leases, acquire cancellation/deadlines, callback accounting, RESET plus protocol/authentication/database restoration, failure discard and replacement |
| Boundary regressions | Unrepresentable timeouts; RESP2 PING fitting a 14-byte budget while RESET needs 15; local REPLCONF rejection without sending or shifting subsequent replies |

The RESET regression requires successful initial acquire/PING, safe discard on
return and a successful replacement acquire/PING. The REPLCONF regression checks
exactly-once IO-lane rejection, no quota poisoning and the exact following ECHO
result. These are formal C++ cases in [redis_pool.cpp](../../../redis_pool.cpp)
and [redis_client.cpp](../../../redis_client.cpp).

[mysql_connection.cpp](../../../mysql_connection.cpp) independently covers
connection destruction from invalid-connect and rejected-command callbacks.
The aggregate admission case uses two encoded queries of at least 33 bytes
against a 64-byte budget; it does not assume small-string optimization or a
specific string capacity. MySQL codec tests explicitly include `<ostream>`
for MSVC's assertion diagnostics.

The [2026-10-13 RUDP/FEC record](../../macmini-m4pro-20261013/rudp-fec/README.md)
retains its own interoperability and sanitizer scope. Its 55-check / 219-case
complete-run totals describe that historical source, not this integration.

## Reproduction

Run from the repository root. Tests are standalone C++ executables; Python or
Ruby is not a test entry point. Use isolated disposable services, never a
production database: MySQL cases create/drop test tables, and Redis cases
temporarily change test keys and an ACL user. Do not run copies against the
same test service concurrently.

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON \
  -DARKNET_BUILD_TESTS=ON -DARKNET_BUILD_BENCHMARKS=ON
cmake --build build-release --parallel 2
```

Start MySQL with the test CA/server identity from `tests/certs`, a disposable
`arknet_tests` schema and a test account allowed to create/drop tables in that
schema. For this run, MySQL listened on `127.0.0.1:34306`. The supplied
certificates are test identities only.

```sh
export ARKNET_MYSQL_TEST_HOST=127.0.0.1
export ARKNET_MYSQL_TEST_PORT=34306
export ARKNET_MYSQL_TEST_USER=arknet_test
export ARKNET_MYSQL_TEST_PASSWORD=arknet-tests-only
export ARKNET_MYSQL_TEST_DATABASE=arknet_tests
export ARKNET_MYSQL_TEST_CA="$PWD/tests/certs/ca.pem"
```

Start Redis with TLS support, the same CA/server identity and the test-only
password `arknet-tests-only`. Provide a TCP listener, a TLS listener without
required client authentication, and a separate TLS listener requiring a client
certificate. Disable persistence and bind loopback. The recorded Redis
sanitizer runs used `26579`, `26580` and `26581`.

```sh
export ARKNET_REDIS_TEST_PORT=26579
export ARKNET_REDIS_TEST_TLS_PORT=26580
export ARKNET_REDIS_TEST_MTLS_PORT=26581
export ARKNET_REDIS_TEST_CA="$PWD/tests/certs/ca.pem"
export ARKNET_REDIS_TEST_CERT="$PWD/tests/certs/client.pem"
export ARKNET_REDIS_TEST_KEY="$PWD/tests/certs/client-key.pem"
cmake --build build-release --target arknet_check
```

Missing service environment variables skip real-server cases. A comparable
full run must report zero skips. Logs are written to `build-release/tests/logs/`.
Stop owned test servers after execution.

For a database-only TSan build, use LLVM 22 and the same service environment:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=/path/to/llvm-22/bin/clang++ \
  -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer -include cfloat' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON \
  -DARKNET_BUILD_TESTS=ON -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=OFF
cmake --build build-tsan --parallel 2 --target \
  arknet_mysql_connection_test arknet_mysql_pool_test \
  arknet_redis_client_test arknet_redis_pool_test
export TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1
build-tsan/tests/arknet_mysql_connection_test
build-tsan/tests/arknet_mysql_pool_test
build-tsan/tests/arknet_redis_client_test
build-tsan/tests/arknet_redis_pool_test
```

RelWithDebInfo supplied effective `-O2 -g -DNDEBUG -std=gnu++20`. The additional
`-include cfloat` handles the LLVM 22/macOS SDK/doctest `DBL_EPSILON` include-order
issue; it is not a sanitizer suppression. MySQL Boost.Asio and Redis Standalone
Asio ASan+UBSan builds use `-fsanitize=address,undefined` for compilation/linking
and effective `-O2`. Both use `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`; MySQL also enables `print_stacktrace=1`.
For Standalone Asio TSan, use a separate build directory with
`ARKNET_USE_BOOST_ASIO=OFF` and `ARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`,
and run the two MySQL targets with the same service environment.
Use separate build directories for sanitizers and for Release performance runs.

## Product Header SHA-256

| Header | SHA-256 |
| --- | --- |
| `mysql/connection.hpp` | `8e88c222abe98b7cc1e5e1aa27053cef8c080ea5c8b9ef01852372e4dca7d1c0` |
| `mysql/pool.hpp` | `44e56ac33457dad19dcd80fccef10cd657acddb35a10ca9d7035435aeeb419bb` |
| `redis/client.hpp` | `952d403284598bbcaeece7373459190bb2773b7c60d46bb95eb66e8b49f1f849` |
| `redis/pool.hpp` | `8cf76f81a872efb477861541580f57a0bb97cb2174cd77995308a6353ae63a47` |

This compact record retains results, environment, commands and source identity.
Raw JSON and bulk machine-local logs are not repository artifacts.
Local correctness and loopback throughput do not establish 100,000–200,000 PCU,
cross-host behavior or production-duration endurance. See
[production acceptance](../../../../docs/project/production-readiness.md) and
[performance methodology](../../../../docs/performance/methodology.md).
