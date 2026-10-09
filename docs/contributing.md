# Contributing

Develop arknet as an independent header-only project. Keep protocol behavior,
IO ownership and public contracts reviewable through focused changes and
reproducible checks.

## Directory Map

| Directory | Responsibility |
| --- | --- |
| `include/arknet/base/` | IO scheduling, lifecycle, event queues and reusable components |
| `include/arknet/tcp/` | TCP and TLS stream endpoints |
| `include/arknet/udp/` | UDP clients, servers, sessions and cast |
| `include/arknet/http/` | HTTP/HTTPS, routing and WebSocket implementation |
| `include/arknet/websocket/` | Public WebSocket headers |
| `include/arknet/external/` | Asio and Beast provider integration |
| `include/arknet/bho/` | Bundled third-party Boost headers |
| `tests/` | doctest cases, integration checks, stress tests and test certificates |
| `benchmarks/` | Callback/coroutine benchmark programs, matrix runner and report tools |
| `examples/` | Small applications demonstrating public APIs |
| `docs/` | English and Chinese Markdown, Docsify and performance pages |
| `cmake/` | Dependency discovery and installed-package configuration |

## Code Style

`.clang-format` defines the C++ style: four spaces, Allman braces, a 120-column
limit and the existing include order. `.editorconfig` defines UTF-8, LF line
endings and indentation for editors. Use clang-format 18 to match CI.

```sh
# Check formatting; --fix applies it.
CLANG_FORMAT=clang-format-18 bash scripts/format.sh --check
CLANG_FORMAT=clang-format-18 bash scripts/format.sh --fix
```

On macOS, install LLVM 18 with `brew install llvm@18` and set
`CLANG_FORMAT="$(brew --prefix llvm@18)/bin/clang-format"`. On Ubuntu 24.04,
install `clang-format-18`. On Windows, use LLVM 18 and run the script in Git Bash.
If `clang-format` already resolves to version 18, omit `CLANG_FORMAT`.

The script checks project headers, tests, examples and benchmark C++ sources.
It excludes bundled Boost headers, doctest and historical measurement snapshots.
CI runs the same check. Formatting tools are development dependencies only.

## Build and Run Tests

See [Install and Configure](guide.md) for dependency setup. From the repository
root, build a development configuration and run the repository check target:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DARKNET_ENABLE_SSL=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
cmake --build build --target arknet_check
```

Functional tests use vendored doctest and are development-only dependencies.
`arknet_check` runs functional programs, stress checks and installed-package
consumption directly. When benchmarks are enabled, it also runs their smoke
cases. Per-program logs are under `build/tests/logs/`. This target does not
use CTest.

Run an individual executable for focused development:

```sh
build/tests/arknet_tcp_test --list-test-cases
build/tests/arknet_tcp_test --test-case="*framing*"
build/tests/arknet_tls_test --reporters=junit --out=build/tls.xml
```

For Visual Studio and other multi-configuration generators, add
`--config Debug` to the CMake build commands and use the generated
configuration subdirectory and `.exe` executable names.

Add tests that reproduce a behavior or failure, including completion counts and
shutdown where applicable. Use explicit synchronization and bounded waits for
asynchronous assertions. Shared component or provider changes need coverage
across both providers, TLS on/off and owned/external IO where affected. Run
sanitizers for lifetime and concurrency changes; run benchmark programs in
separate Release builds. The current checked configurations and remaining
coverage are recorded in [Testing](testing.md).

## Benchmark and Report Changes

Benchmark sources belong in `benchmarks/` and remain runnable by library users.
Keep them independent of doctest. Compare identical hardware, dependencies,
compiler settings and workloads; do not run benchmarks alongside builds or
other tests.

The standard procedure, command options and measurement definitions are in
[Testing](testing.md). [IO Models](threading.md) explains shared-context and
sharded-context comparisons, including native Asio coroutine workloads.
The optional report generator's packages are listed in
`benchmarks/requirements-report.txt`.

Retain raw JSON, exact commands, host metadata, source revision and binary
hashes with a report. Separate short smoke checks from performance evidence.
Do not infer production capacity from loopback throughput or compare callback
message RTT with coroutine batch RTT as if they were the same metric.

## Documentation and Commit History

Every documentation page has an English and Chinese version. The Docsify page
header provides the language dropdown. Update both versions together. Keep examples, commands,
defaults, limitations and status equivalent. Place detailed material under
`docs/` and keep the root README concise. New pages also need navigation entries
and working relative links in Docsify and on GitHub.

Use English commit messages. Before submission, squash work-in-progress commits
into coherent, reviewed changes. Describe the final behavior and validation in
the commit or pull request; do not retain intermediate debugging history.

## Third-Party Notices

The project uses BSL-1.0. New independently written arknet core code uses
OpenArkStudio's notice. Preserve applicable notices in bundled third-party
files, including BHO and doctest. Record newly bundled dependencies in both
[third-party notice pages](THIRD_PARTY_NOTICES.md), including their license files where required.

Changing names, formatting or a language standard does not remove a copied
implementation's license obligations. Keep third-party modifications isolated
and explain their purpose. Development and documentation dependencies must not
become runtime requirements for applications using arknet.
