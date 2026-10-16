[English](README.md) | [简体中文](README_CN.md)

# Compare library revisions with one benchmark driver

This optional standalone CMake project builds the formal benchmark programs from one source checkout against another arknet checkout. Keep the driver checkout identical for both versions. The normal `ARKNET_BUILD_BENCHMARKS=ON` entry remains available for measuring a single checkout.

Requirements: CMake 3.21+, a C++20 compiler, and the dependencies required by the selected library checkout. For the revisions below, use Boost 1.90+ or Standalone Asio 1.38+, with OpenSSL 1.1.1+ for TLS and MySQL. Dependencies must already be installed; this project does not fetch them. No Python, Ruby, or jq is required to build or run the programs.

## Build the version comparison

Prepare local checkouts at these exact revisions. The shared driver contains the loopback cleanup order used in the published comparison.

| Input | Revision |
| --- | --- |
| Released 0.2.0 library | `25fe2e5fac4e09edee597f312d58b0362b9e0681` |
| Measured shutdown implementation | `2aa5bc0ef7d383d8cfbc04520f3b952fa27c5fbd` |
| Shared benchmark driver | `2aa5bc0ef7d383d8cfbc04520f3b952fa27c5fbd` |

Replace the absolute checkout and build paths below. Both commands select Boost and TLS; the `comparison` source path is this directory.

```sh
cmake -S /path/to/arknet/benchmarks/comparison -B /path/to/build-v020 \
  -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_LIBRARY_SOURCE=/path/to/arknet-v020 \
  -DARKNET_BENCHMARK_DRIVER_SOURCE=/path/to/arknet-2aa5bc0 \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
cmake --build /path/to/build-v020 --config Release --parallel 1

cmake -S /path/to/arknet/benchmarks/comparison -B /path/to/build-2aa5bc0 \
  -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_LIBRARY_SOURCE=/path/to/arknet-2aa5bc0 \
  -DARKNET_BENCHMARK_DRIVER_SOURCE=/path/to/arknet-2aa5bc0 \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
cmake --build /path/to/build-2aa5bc0 --config Release --parallel 1
```

To select Standalone Asio, use a separate build directory and replace the dependency option with `-DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`. HTTP and WebSocket measurements require Boost. With `ARKNET_ENABLE_SSL=OFF`, the MySQL target is omitted and secure transports are unavailable.

The executables are in `BUILD/benchmarks` with single-configuration generators. Visual Studio and other multi-configuration generators add a configuration directory, such as `BUILD/benchmarks/Release`; Windows executables have the `.exe` suffix.

```sh
/path/to/build-v020/benchmarks/arknet_loopback_benchmark --help
/path/to/build-v020/benchmarks/arknet_coroutine_benchmark --help
/path/to/build-v020/benchmarks/arknet_redis_benchmark --help
/path/to/build-v020/benchmarks/arknet_mysql_benchmark --help
```

Use the same commands for the second build. A successful `--help` checks the executable entry only; it does not exercise a server or measure performance.

## Run with an explicit scope

The programs retain their original options and JSON-lines output:

- `loopback`: public arknet transports; client and server CPU are measured in the same process. Pass `--certs /path/to/arknet-2aa5bc0/tests/certs` for local TLS loopback tests. These bundled certificates are test resources.
- `coroutine`: native Asio TCP callback/coroutine control. It does not measure a public arknet coroutine endpoint.
- `redis`: binary ECHO against an existing isolated Redis-compatible server. Latency covers a complete pipeline batch; CPU covers only the client process.
- `mysql`: read-only text `SELECT` or prepared `SELECT ?` against an existing isolated MySQL server. CPU covers only the client process.

For database TLS, use a CA that trusts the test server and a host matching its certificate. MySQL verifies TLS by default with `--ca FILE`; Redis requires `--tls --ca FILE`. Set passwords through `ARKNET_MYSQL_PASSWORD` and `ARKNET_REDIS_PASSWORD` environment variables; use `--username` and MySQL `--database` for the other account settings. Do not disable verification to make a test pass. See the [MySQL guide](../../docs/features/mysql/usage.md) and [Redis guide](../../docs/features/redis/usage.md) for connection setup.

For a comparison, keep compiler, dependencies, TLS, payload, connection count, in-flight requests, warmup, duration, and machine load identical. Run versions next to each other, alternate their order, and repeat each group in fresh processes. Save the complete CLI, source/driver revisions, source and executable hashes, environment, error counts, throughput, latency, CPU, and RSS outside the source tree. One fully occupied CPU core is 100%. Do not combine the native Asio control with public transport or database measurements into one throughput number.

This build entry does not start services, run a measurement grid, summarize results, or download historical revisions. See the [database comparison](../../docs/performance/database-comparison.md), [transport comparison](../../docs/performance/transport-comparison.md), and [Asio execution control](../../docs/performance/asio-execution-control.md) for the measured scope and results.
