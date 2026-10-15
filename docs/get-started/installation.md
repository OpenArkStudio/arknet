# Install and Configure

arknet is header-only and has no binary library to build or link.
The `arknet::arknet` CMake interface target supplies include paths, C++20 requirements,
compile definitions and dependencies. Applications link the platform's socket and
thread libraries, plus OpenSSL when TLS is enabled.

## Requirements

- CMake 3.21 or newer.
- A compiler and standard library with C++20 support.
- Standalone Asio 1.38 or newer, or Boost 1.90 or newer with Asio and Beast.
- OpenSSL 1.1.1 or newer for TLS, HTTPS and WSS.

The configured minimum TLS version is 1.2. Tested dependency and compiler
versions are recorded in [Testing](../project/testing.md).

<Aside type="note" title="Note: Required Asio version">

The Asio minimum includes the kqueue descriptor-publication synchronization
fix required for multi-threaded IO on macOS. CMake and direct-header builds
reject older providers. See [Asio's revision history](https://think-async.com/Asio/asio-1.38.2/doc/asio/history.html).

</Aside>

## Choose an Asio Provider

| Configuration | IO dependencies | Available protocols |
| --- | --- | --- |
| `ARKNET_USE_BOOST_ASIO=ON` (default) | Boost.Asio + Boost.Beast | TCP, UDP, RUDP, HTTP, WebSocket; optional TLS, HTTPS, WSS |
| `ARKNET_USE_BOOST_ASIO=OFF` | Standalone Asio | TCP, UDP, RUDP; optional TLS |

HTTP, WebSocket and rate-limited TCP streams require official Boost.Beast and
the Boost configuration. These interfaces are unavailable with Standalone Asio;
including their headers directly reports a compile error. Install the selected
dependencies before configuring CMake. Set `ARKNET_ASIO_INCLUDE_DIR` to the
directory containing `asio.hpp` for Standalone Asio. For custom Boost or OpenSSL
prefixes, use `CMAKE_PREFIX_PATH`; OpenSSL also accepts `OPENSSL_ROOT_DIR`.

## CMake Options

See the [configuration reference](../reference/configuration.md#cmake-options) for all options and defaults.

<Steps>

<Step title="Install the selected dependencies">

Install one Asio provider and OpenSSL if you need TLS. Choose a provider using the table above.

</Step>

<Step title="Configure the project">

From the repository root, run the command for your selected provider:

```sh
# Standalone Asio
cmake -S . -B build -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include

# Boost.Asio with secure transports
cmake -S . -B build-boost -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
```

</Step>

<Step title="Build and verify">

Then build the examples and tests in the selected directory:

```sh
cmake --build build --parallel 3
```

For a multi-configuration generator such as Visual Studio, add `--config Release`
to build and install commands. See [Contributing](../project/contributing.md) for test
execution and [Testing](../project/testing.md) for correctness evidence; see [measurement methodology](../performance/methodology.md) for benchmarks.

</Step>

</Steps>

## Integrate From Source

Add the independent arknet checkout to your application's CMake project:

```cmake
add_subdirectory(/path/to/arknet arknet-build)
target_link_libraries(my_app PRIVATE arknet::arknet)
```

For an `INTERFACE` target, `target_link_libraries()` propagates its usage requirements.
It does not add an arknet `.a`, `.so`, `.dylib` or `.lib` to the linker input.

Set the provider and TLS options before `add_subdirectory()`, or pass them to
CMake on the command line. Tests and examples default to disabled when arknet is
embedded. Include `<arknet/arknet.hpp>` or the required protocol header.
Continue with [Quick Start](first-application.md).

## Install and Use find_package

Configure the library with the provider and TLS settings required by the
application, then install its headers and CMake package:

```sh
cmake -S . -B build-install -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_USE_BOOST_ASIO=ON
cmake --install build-install --prefix /path/to/prefix
```

In the consuming project:

```cmake
find_package(arknet CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE arknet::arknet)
```

Configure that project with `-DCMAKE_PREFIX_PATH=/path/to/prefix`. The installed
package remembers the provider and TLS settings used at installation and finds
those dependencies on the consuming machine. Install separate prefixes for
configurations that need different providers or TLS settings.

## Use vcpkg

The development `vcpkg.json` enables Boost by default and also installs Standalone Asio for the two-configuration test matrix. The `boost` feature is enabled by default; the optional `ssl` feature installs OpenSSL. Features
install dependencies; the corresponding arknet CMake options select behavior.
`vcpkg-configuration.json` selects the repository's `ports/asio` overlay, pinned
to Asio 1.38.2 with an archive checksum. The selected registry baseline still
provides Asio 1.32, which lacks the required kqueue synchronization. Consuming
arknet from another vcpkg manifest requires that project to configure this
overlay or provide a compatible Asio installation.

```sh
cmake -S . -B build-vcpkg \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_MANIFEST_FEATURES="boost;ssl" \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
cmake --build build-vcpkg --parallel 3
```

## Integrate Headers Directly

Add `include/` and the chosen provider's include directory to your compiler
search paths, compile as C++20, and link platform socket/thread dependencies.
Secure transports also require OpenSSL's headers and libraries. On Windows,
link `ws2_32` and `mswsock`. The CMake target additionally sets `/bigobj`
for MSVC.

Define configuration macros before including arknet or provider headers:

See [direct-header macros](../reference/configuration.md#direct-header-macros) for their meanings.

<Aside type="caution" title="Warning: Keep build settings consistent">

Use identical provider, TLS and behavior macros in every translation unit of a
program. Prefer `arknet::arknet` when using CMake so configuration propagates
to consumers automatically.

</Aside>
