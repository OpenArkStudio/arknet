# Usage Example Validation

[English](README.md) | [中文](README_CN.md)

The TCP, UDP, WebSocket, TLS and HTTP Usage pages were extracted and checked on
2026-10-10. Their English and Chinese C++ blocks match exactly. The two programs
from each page and the documented HTTPS/WSS equivalents form 14 executables.

| Backend | Dependency | Compiled programs | Passed client/server pairs |
| --- | --- | ---: | ---: |
| Standalone | Asio 1.38.2 | 14 | 7 |
| Boost | Boost 1.90.0 | 14 | 7 |

Both configurations used AppleClang 21.0.0, OpenSSL 3.6.4, Release and TLS ON.
Each client checked echoed content; WebSocket also checked binary message type,
and HTTP checked status 200. All 28 processes returned zero. Servers stopped
normally after Enter, and the runner reaped them without a timeout or forced kill.
This record covers macOS; it is not a Linux or Windows validation result.

`standalone/` and `boost/` retain configure/build logs, each process's stdout and
stderr, and `summary.json`. `prepare.py` and `run.py` preserve the scripts used
to collect these historical records; they are not current test entry points.
Current tests and benchmarks run compiled C++ programs directly. Batch measurements
use `scripts/benchmark_matrix.sh`; see [Testing](../../../../docs/testing.md).

## Historical Collection Commands

Run from the repository root. These commands use the dependency paths on the
validation machine; replace those paths on another host. Run the two backend
checks sequentially because their examples use the same listening ports.

```sh
python3 -B tests/results/macmini-m4pro-20261010/usage/prepare.py \
  --arknet . --output /private/tmp/arknet-usage-check/project

cmake -S /private/tmp/arknet-usage-check/project \
  -B /private/tmp/arknet-usage-check/build-standalone -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
  -DARKNET_ENABLE_SSL=ON -DARKNET_USE_BOOST_ASIO=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/private/tmp/arknet-asio-1.38.2/include \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
  > /private/tmp/arknet-usage-check/standalone-configure.log 2>&1
cmake --build /private/tmp/arknet-usage-check/build-standalone --parallel 3 \
  > /private/tmp/arknet-usage-check/standalone-build.log 2>&1
python3 -B tests/results/macmini-m4pro-20261010/usage/run.py \
  --arknet . --build /private/tmp/arknet-usage-check/build-standalone \
  --logs /private/tmp/arknet-usage-check/logs-standalone

cmake -S /private/tmp/arknet-usage-check/project \
  -B /private/tmp/arknet-usage-check/build-boost -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
  -DARKNET_ENABLE_SSL=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DBoost_DIR=/opt/homebrew/lib/cmake/Boost-1.90.0 \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
  > /private/tmp/arknet-usage-check/boost-configure.log 2>&1
cmake --build /private/tmp/arknet-usage-check/build-boost --parallel 3 \
  > /private/tmp/arknet-usage-check/boost-build.log 2>&1
python3 -B tests/results/macmini-m4pro-20261010/usage/run.py \
  --arknet . --build /private/tmp/arknet-usage-check/build-boost \
  --logs /private/tmp/arknet-usage-check/logs-boost
```
