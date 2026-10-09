# 安装和配置

arknet 是 header-only 网络库，没有需要编译或链接的二进制库。
`arknet::arknet` CMake 接口目标传递头文件路径、C++20 要求、编译宏和依赖。
应用仍需链接平台的 socket 库和线程库；启用 TLS 时还需链接 OpenSSL。

## 环境要求

- CMake 3.21 或更新版本。
- 支持 C++20 的编译器和标准库。
- standalone Asio 1.38 或更新版本，或包含 Asio 和 Beast 的 Boost 1.90 或更新版本。
- TLS、HTTPS 和 WSS 需要 OpenSSL 1.1.1 或更新版本。

库配置的最低 TLS 版本为 1.2。已验证的依赖和编译器版本见[测试](testing_CN.md)。

Asio 版本下限包含 macOS 多线程 IO 所需的 kqueue descriptor 发布同步修复。
CMake 和直接包含头文件的构建都会拒绝较旧后端。修复记录见
[Asio 版本历史](https://think-async.com/Asio/asio-1.38.2/doc/asio/history.html)。

## 选择 Asio 后端

| 配置 | IO 实现 | HTTP 和 WebSocket 实现 |
| --- | --- | --- |
| `ARKNET_USE_BOOST_ASIO=OFF`（默认） | standalone Asio | 基于 Boost 1.84 适配的内置 BHO Beast |
| `ARKNET_USE_BOOST_ASIO=ON` | Boost.Asio | 官方 Boost.Beast |

两种配置提供相同的 arknet API。配置 CMake 前，安装所选后端的依赖。
standalone 模式会查找 `asio.hpp`；如果头文件不在搜索路径内，设置
`ARKNET_ASIO_INCLUDE_DIR`。Boost 或 OpenSSL 安装在自定义前缀时，使用
`CMAKE_PREFIX_PATH`；OpenSSL 也支持 `OPENSSL_ROOT_DIR`。

### BHO 是什么

BHO 是 **Boost Header Only** 的缩写。`include/arknet/bho/` 目录存放使用
`bho` 命名空间的适配版 Boost 头文件，包括 Beast 及其所需组件。这些头文件让
standalone Asio 无须单独安装 Boost 即可使用 HTTP 和 WebSocket。

官方 Boost.Beast 使用 Boost.Asio socket 类型。内置 BHO Beast 适配了 standalone
Asio；Boost 模式使用官方 Boost.Beast。BHO 是内置兼容代码，不是第三种后端，也
无须安装额外软件包。许可证声明见
[第三方声明](THIRD_PARTY_NOTICES_CN.md)。

## CMake 选项

| 选项 | 默认值 | 作用 |
| --- | --- | --- |
| `ARKNET_USE_BOOST_ASIO` | `OFF` | 使用 Boost.Asio 和 Boost.Beast |
| `ARKNET_ENABLE_SSL` | `OFF` | 启用 TLS、HTTPS 和 WSS，并链接 OpenSSL |
| `ARKNET_BUILD_TESTS` | 独立构建为 `ON`，嵌入构建为 `OFF` | 构建 doctest 和集成检查 |
| `ARKNET_BUILD_EXAMPLES` | 独立构建为 `ON`，嵌入构建为 `OFF` | 构建示例程序 |
| `ARKNET_BUILD_BENCHMARKS` | `OFF` | 构建正式基准测试程序 |

在仓库根目录选择一种后端：

```sh
# standalone Asio
cmake -S . -B build -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include

# Boost.Asio 和安全传输
cmake -S . -B build-boost -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
```

然后构建所选目录中的示例和测试程序：

```sh
cmake --build build --parallel 3
```

使用 Visual Studio 等多配置生成器时，为构建和安装命令添加 `--config Release`。
测试执行方式见[贡献指南](contributing_CN.md)，性能测量方式见[测试](testing_CN.md)。

## 从源码接入

在应用的 CMake 项目中引用独立的 arknet 源码目录：

```cmake
add_subdirectory(/path/to/arknet arknet-build)
target_link_libraries(my_app PRIVATE arknet::arknet)
```

对 `INTERFACE` 目标，`target_link_libraries()` 用于传递使用该库所需的构建配置。
它不会向链接器添加 arknet 的 `.a`、`.so`、`.dylib` 或 `.lib` 文件。

在 `add_subdirectory()` 前设置后端和 TLS 选项，或通过 CMake 命令行传入。
嵌入构建默认关闭测试和示例。代码可以包含 `<arknet/arknet.hpp>` 或所需协议的头文件。
接入后继续阅读[快速开始](quickstart_CN.md)。

## 安装和 find_package

使用应用所需的后端和 TLS 配置生成安装包，然后安装头文件和 CMake 包：

```sh
cmake -S . -B build-install -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --install build-install --prefix /path/to/prefix
```

使用安装包的项目配置如下：

```cmake
find_package(arknet CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE arknet::arknet)
```

配置应用时传入 `-DCMAKE_PREFIX_PATH=/path/to/prefix`。安装包会保留安装时的后端
和 TLS 配置，并在使用端查找这些依赖。需要不同后端或 TLS 配置时，使用不同安装前缀。

## 使用 vcpkg

仓库的 `vcpkg.json` 默认安装 standalone Asio。可选的 `boost` 和 `ssl`
feature 分别安装 Boost.Asio/Beast 和 OpenSSL。feature 用于安装依赖；arknet 的对应
CMake 选项用于选择行为。
`vcpkg-configuration.json` 指定仓库中的 `ports/asio` overlay，以源码包校验和固定
Asio 1.38.2。所选 registry baseline 仍提供 Asio 1.32，缺少所需的 kqueue 同步。
从另一个 vcpkg manifest 使用 arknet 时，需要由应用项目配置此 overlay，或提供
兼容版本的 Asio 安装。

```sh
cmake -S . -B build-vcpkg \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_MANIFEST_FEATURES="boost;ssl" \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
cmake --build build-vcpkg --parallel 3
```

## 直接使用头文件

将 `include/` 和所选后端的头文件目录加入编译器搜索路径，以 C++20 编译，并链接
平台的 socket 和线程依赖。安全传输还需要 OpenSSL 头文件和库。Windows 下链接
`ws2_32` 和 `mswsock`。CMake 目标还会为 MSVC 添加 `/bigobj`。

在包含 arknet 或后端头文件前定义配置宏：

| 宏 | 直接使用头文件时的含义 |
| --- | --- |
| `ARKNET_USE_BOOST_ASIO=1` | 使用 Boost；未定义或设为 `0` 时使用 standalone Asio |
| `ARKNET_ENABLE_SSL` | 定义即启用安全传输；关闭时不要定义此宏 |
| `ARKNET_HEADER_ONLY` | 内部后端宏，不用于切换编译库 |

同一程序的全部翻译单元必须使用相同的后端、TLS 和行为宏。使用 CMake 时，优先
使用 `arknet::arknet` 接口目标，由目标统一传递配置。
