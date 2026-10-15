# 安装和配置

arknet 是 header-only 网络库，没有需要编译或链接的二进制库。
`arknet::arknet` CMake 接口目标传递头文件路径、C++20 要求、编译宏和依赖。
应用仍需链接平台的 socket 库和线程库；MySQL 认证与 TLS 还需链接 OpenSSL。

## 环境要求

- CMake 3.21 或更新版本。
- 支持 C++20 的编译器和标准库。
- 默认使用包含 Asio 和 Beast 的 Boost 1.90 或更新版本；部分能力也可使用 Standalone Asio 1.38 或更新版本。
- MySQL 认证与安全传输需要 OpenSSL 1.1.1 或更新版本。

库配置的最低 TLS 版本为 1.2。已验证的依赖和编译器版本见[测试](../project/testing_CN.md)。

<Aside type="note" title="说明：Asio 版本要求">

Asio 版本下限包含 macOS 多线程 IO 所需的 kqueue descriptor 发布同步修复。
CMake 和直接包含头文件的构建都会拒绝较旧依赖版本。修复记录见
[Asio 版本历史](https://think-async.com/Asio/asio-1.38.2/doc/asio/history.html)。

</Aside>

## 选择 Asio 依赖方案

| 配置 | IO 实现 | 可用协议 |
| --- | --- | --- |
| `ARKNET_USE_BOOST_ASIO=ON`（默认） | Boost.Asio + Boost.Beast | TCP、UDP、RUDP、HTTP、WebSocket、Redis；可选 MySQL、TLS、HTTPS、WSS |
| `ARKNET_USE_BOOST_ASIO=OFF` | Standalone Asio | TCP、UDP、RUDP、Redis；可选 MySQL 和 TLS |

HTTP、WebSocket 和 TCP 限速流需要官方 Boost.Beast，因此必须选择 Boost。
Standalone Asio 不提供这些接口；直接包含相关头文件会给出编译错误。
配置 CMake 前安装对应依赖库。Standalone Asio 模式通过 `ARKNET_ASIO_INCLUDE_DIR`
指定 `asio.hpp` 所在目录；Boost 或 OpenSSL 使用自定义安装前缀时，设置
`CMAKE_PREFIX_PATH`。OpenSSL 也支持 `OPENSSL_ROOT_DIR`。

## CMake 选项

完整选项与默认值见[配置参考](../reference/configuration_CN.md#cmake-选项)。

<Steps>

<Step title="安装依赖库">

按上表选择并安装一种 Asio 依赖方案。需要 MySQL 或 TLS 时，同时安装 OpenSSL。

</Step>

<Step title="配置项目">

在仓库根目录执行对应依赖方案的配置命令：

```sh
# Standalone Asio
cmake -S . -B build -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include

# Boost.Asio 和安全传输
cmake -S . -B build-boost -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
```

</Step>

<Step title="构建和验证">

然后构建所选目录中的示例和测试程序：

```sh
cmake --build build --parallel 3
```

使用 Visual Studio 等多配置生成器时，为构建和安装命令添加 `--config Release`。
测试执行方式见[贡献指南](../project/contributing_CN.md)，性能测量方式见[测量方法](../performance/methodology_CN.md)。

</Step>

</Steps>

## 从源码接入

在应用的 CMake 项目中引用独立的 arknet 源码目录：

```cmake
add_subdirectory(/path/to/arknet arknet-build)
target_link_libraries(my_app PRIVATE arknet::arknet)
```

对 `INTERFACE` 目标，`target_link_libraries()` 用于传递使用该库所需的构建配置。
它不会向链接器添加 arknet 的 `.a`、`.so`、`.dylib` 或 `.lib` 文件。

在 `add_subdirectory()` 前设置依赖方案和 TLS 选项，或通过 CMake 命令行传入。
嵌入构建默认关闭测试和示例。代码可以包含 `<arknet/arknet.hpp>` 或所需协议的头文件。
接入后继续阅读[快速开始](first-application_CN.md)。

## 安装和 find_package

使用应用所需的依赖方案和 TLS 配置生成安装包，然后安装头文件和 CMake 包：

```sh
cmake -S . -B build-install -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_USE_BOOST_ASIO=ON
cmake --install build-install --prefix /path/to/prefix
```

使用安装包的项目配置如下：

```cmake
find_package(arknet CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE arknet::arknet)
```

配置应用时传入 `-DCMAKE_PREFIX_PATH=/path/to/prefix`。安装包会保留安装时的依赖方案
和 TLS 配置，并在使用端查找这些依赖。需要不同依赖方案或 TLS 配置时，使用不同安装前缀。

## 使用 vcpkg

仓库的 `vcpkg.json` 默认启用 Boost，并安装 Standalone Asio 以运行两种依赖方案的测试。`boost` feature 默认启用，可选的 `ssl` feature 安装 OpenSSL。feature 用于安装依赖；arknet 的对应
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

将 `include/` 和所选依赖库的头文件目录加入编译器搜索路径，以 C++20 编译，并链接
平台的 socket 和线程依赖。MySQL 连接与安全传输还需要 OpenSSL 头文件和库。Windows 下链接
`ws2_32` 和 `mswsock`，并在包含 Windows 头文件前定义 `NOMINMAX`。
CMake 目标会传递 `NOMINMAX`，并为 MSVC 添加 `/bigobj`。

在包含 arknet 或依赖库头文件前定义配置宏：

宏的含义见[直接使用头文件时的宏](../reference/configuration_CN.md#直接使用头文件时的宏)。

<Aside type="caution" title="注意：保持构建配置一致">

同一程序的全部翻译单元必须使用相同的依赖方案、TLS 和行为宏。使用 CMake 时，优先
使用 `arknet::arknet` 接口目标，由目标统一传递配置。

</Aside>
