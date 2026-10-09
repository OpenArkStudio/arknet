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
versions are recorded in [Testing](testing.md).

The Asio minimum includes the kqueue descriptor-publication synchronization
fix required for multi-threaded IO on macOS. CMake and direct-header builds
reject older providers. See [Asio's revision history](https://think-async.com/Asio/asio-1.38.2/doc/asio/history.html).

## Choose an Asio Provider

| Configuration | IO implementation | HTTP and WebSocket implementation |
| --- | --- | --- |
| `ARKNET_USE_BOOST_ASIO=OFF` (default) | Standalone Asio | Bundled BHO Beast, adapted from Boost 1.84 |
| `ARKNET_USE_BOOST_ASIO=ON` | Boost.Asio | Official Boost.Beast |

Both configurations expose the same arknet API. Install the dependencies for the
chosen provider before configuring CMake. For standalone Asio, CMake searches
for `asio.hpp`; set `ARKNET_ASIO_INCLUDE_DIR` if it is outside the search paths.
For Boost or OpenSSL in a custom prefix, use `CMAKE_PREFIX_PATH`; OpenSSL also
accepts `OPENSSL_ROOT_DIR`.

### What Is BHO?

BHO means **Boost Header Only**. The `include/arknet/bho/` directory contains
adapted Boost headers under the `bho` namespace, including Beast and its
supporting components. These headers let standalone Asio use HTTP and WebSocket
without a separate Boost installation.

Official Boost.Beast uses Boost.Asio socket types. The bundled BHO Beast is
adapted to standalone Asio. In Boost mode, arknet uses official Boost.Beast.
BHO is bundled compatibility code; it is not a third provider or an additional
package to install. Its notices are listed in
[Third-Party Notices](THIRD_PARTY_NOTICES.md).

## CMake Options

| Option | Default | Effect |
| --- | --- | --- |
| `ARKNET_USE_BOOST_ASIO` | `OFF` | Select Boost.Asio and Boost.Beast |
| `ARKNET_ENABLE_SSL` | `OFF` | Enable TLS, HTTPS and WSS; link OpenSSL |
| `ARKNET_BUILD_TESTS` | `ON` at the top level, `OFF` when embedded | Build doctest and integration checks |
| `ARKNET_BUILD_EXAMPLES` | `ON` at the top level, `OFF` when embedded | Build example applications |
| `ARKNET_BUILD_BENCHMARKS` | `OFF` | Build the permanent benchmark executables |

From the repository root, configure one provider:

```sh
# Standalone Asio
cmake -S . -B build -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include

# Boost.Asio with secure transports
cmake -S . -B build-boost -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
```

Then build the examples and tests in the selected directory:

```sh
cmake --build build --parallel 3
```

For a multi-configuration generator such as Visual Studio, add `--config Release`
to build and install commands. See [Contributing](contributing.md) for test
execution and [Testing](testing.md) for performance measurements.

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
Continue with [Quick Start](quickstart.md).

## Install and Use find_package

Configure the library with the provider and TLS settings required by the
application, then install its headers and CMake package:

```sh
cmake -S . -B build-install -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
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

The repository's `vcpkg.json` installs standalone Asio by default. Its optional
`boost` and `ssl` features install Boost.Asio/Beast and OpenSSL. Features
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

| Macro | Direct-header meaning |
| --- | --- |
| `ARKNET_USE_BOOST_ASIO=1` | Select Boost; undefined or `0` selects standalone Asio |
| `ARKNET_ENABLE_SSL` | Its presence enables secure transports; leave it undefined to disable them |
| `ARKNET_HEADER_ONLY` | Internal provider macro, not a switch for a compiled library |

Use identical provider, TLS and behavior macros in every translation unit of a
program. Prefer `arknet::arknet` when using CMake so configuration propagates
to consumers automatically.
