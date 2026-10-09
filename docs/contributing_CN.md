# 贡献指南

arknet 作为独立的 header-only 项目开发。通过范围明确的修改和可复现的检查，让
协议行为、IO 所有权和公共接口约定便于审查。

## 目录职责

| 目录 | 职责 |
| --- | --- |
| `include/arknet/base/` | IO 调度、生命周期、事件队列和可复用组件 |
| `include/arknet/tcp/` | TCP 和 TLS 流端点 |
| `include/arknet/udp/` | UDP 客户端、服务端、Session 和 cast |
| `include/arknet/http/` | HTTP/HTTPS、路由和 WebSocket 实现 |
| `include/arknet/websocket/` | 公共 WebSocket 头文件 |
| `include/arknet/external/` | Asio 和 Beast 后端接入 |
| `include/arknet/bho/` | 内置的第三方 Boost 头文件 |
| `tests/` | doctest、集成检查、压力测试和测试证书 |
| `benchmarks/` | 正式的回调及协程基准程序、矩阵执行器和报告工具 |
| `examples/` | 演示公共 API 的小型应用 |
| `docs/` | 中英文 Markdown、Docsify 和性能页面 |
| `cmake/` | 依赖查找和安装包配置 |

## 代码风格

`.clang-format` 定义 C++ 格式：4 空格缩进、Allman 括号、120 列上限，并保留原有
include 顺序。`.editorconfig` 定义编辑器的 UTF-8 编码、LF 换行和缩进约定。
使用 clang-format 18，与 CI 保持一致。

```sh
# 检查格式；--fix 应用格式。
CLANG_FORMAT=clang-format-18 bash scripts/format.sh --check
CLANG_FORMAT=clang-format-18 bash scripts/format.sh --fix
```

macOS 使用 `brew install llvm@18` 安装，并设置
`CLANG_FORMAT="$(brew --prefix llvm@18)/bin/clang-format"`。Ubuntu 24.04 安装
`clang-format-18`。Windows 安装 LLVM 18，在 Git Bash 中运行脚本。
如果 `clang-format` 命令已经指向版本 18，可以省略 `CLANG_FORMAT`。

脚本检查项目头文件、测试、示例和基准测试的 C++ 源码，排除内置 Boost 头文件、
doctest 和历史测量快照。CI 执行相同检查。格式工具仅用于开发。

## 构建和测试

依赖配置见[安装和配置](guide_CN.md)。在仓库根目录构建开发配置，并运行检查目标：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DARKNET_ENABLE_SSL=ON \
  -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
cmake --build build --parallel 3
cmake --build build --target arknet_check
```

功能测试使用内置 doctest，仅作为开发依赖。`arknet_check` 直接运行功能程序、
压力检查和安装包使用检查；启用 benchmark 后还会运行基准 smoke 检查。每个程序
的日志位于 `build/tests/logs/`。该目标不使用 CTest。

开发时可以单独运行所需测试程序：

```sh
build/tests/arknet_tcp_test --list-test-cases
build/tests/arknet_tcp_test --test-case="*framing*"
build/tests/arknet_tls_test --reporters=junit --out=build/tls.xml
```

使用 Visual Studio 等多配置生成器时，在 CMake 构建命令中添加 `--config Debug`，
并使用对应配置的输出子目录和带 `.exe` 扩展名的程序。

新增测试应复现行为或失败，必要时检查完成次数和停止结果。异步断言使用明确的
同步机制和有时限的等待。修改共享组件或后端时，应覆盖受影响的两种后端、TLS
开关及内置和外部 IO。生命周期和并发修改使用 sanitizer 检查；性能测试使用独立
的 Release 构建。当前检查配置和未覆盖范围见[测试](testing_CN.md)。

## 基准测试和报告

基准源码放在 `benchmarks/`，让库的使用者能够自行运行，并保持与 doctest 独立。
比较时使用相同硬件、依赖、编译配置和工作负载；基准测试期间不要同时构建或运行
其他测试。

标准流程、参数和测量定义见[测试](testing_CN.md)。[IO 模型](threading_CN.md)
说明共享 context 和分片 context 的比较，包括原生 Asio 协程工作负载。可选报告
生成器的依赖列在 `benchmarks/requirements-report.txt`。

报告保留原始 JSON、实际命令、宿主元数据、源码版本和程序哈希。区分短时 smoke
检查与性能证据。不能根据回环吞吐推断生产容量，也不能将回调的单消息 RTT 和
协程的批次 RTT 当作相同指标比较。

## 文档和提交历史

每个文档页面都有英文和中文版本，Docsify 页面顶部通过下拉框选择语言。修改时同步更新两个版本，
确保示例、命令、默认值、限制和状态一致。详细内容放在 `docs/`，根 README 保持
简洁。新增页面需要导航入口，以及在 Docsify 和 GitHub 中都可用的相对链接。

提交信息使用英文。提交前，将开发过程中的临时 commit squash 为经过审查的连贯
修改。commit 或 pull request 说明最终行为和验证结果，不保留中间调试历史。

## 第三方声明

项目使用 BSL-1.0。独立编写的 arknet 核心代码使用 OpenArkStudio 声明。保留内置
第三方文件的适用声明，包括 BHO 和 doctest。新增内置依赖时，同步更新中英文
[第三方声明](THIRD_PARTY_NOTICES_CN.md)，并按许可证要求保留许可证文件。

修改名称、格式或语言标准不会消除复制实现的许可证义务。第三方修改应独立维护，
并说明修改目的。开发和文档依赖不应成为使用 arknet 的应用运行依赖。
