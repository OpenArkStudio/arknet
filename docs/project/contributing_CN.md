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
| `include/arknet/external/` | Asio 和 Beast 依赖接入 |
| `tests/` | doctest、集成检查、压力测试和测试证书 |
| `benchmarks/` | 正式的回调及协程基准程序、矩阵执行器和报告工具 |
| `examples/` | 演示公共 API 的小型应用 |
| `docs/` | 中英文 Markdown、Nimbus 站点和性能报告 |
| `cmake/` | 依赖查找和安装包配置 |

## 代码风格

`.clang-format` 定义 C++ 格式：4 空格缩进、Allman 括号、120 列上限。
include 按系统库、第三方库、项目自身分组，组间空一行；组内按名称排序。
项目公共头文件先于本地头文件，`windows.h` 先于依赖它的系统头文件。
条件编译和配置宏的先后关系保持不变。`.editorconfig` 定义 UTF-8 编码、LF 换行和缩进约定。
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

以 `main` 为目标分支的草稿 PR 转为可审查状态后，等待当前 head 的新一轮
`ready_for_review` CI。申请进入合并队列前，核对这轮源分支检查；此前的检查不能替代它。
详见 [CI 覆盖范围](testing_CN.md#ci-覆盖范围)。

依赖配置见[安装和配置](../get-started/installation_CN.md)。在仓库根目录构建开发配置，并运行检查目标：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DARKNET_ENABLE_SSL=ON \
  -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include
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
同步机制和有时限的等待。修改共享组件或依赖接入层时，应覆盖受影响的两种依赖方案、TLS
开关及内置和外部 IO。生命周期和并发修改使用 sanitizer 检查；性能测试使用独立
的 Release 构建。当前检查配置和未覆盖范围见[测试](testing_CN.md)。

`CI - required` 采集终结状态超时时，保留失败运行，并按
[CI 覆盖范围](testing_CN.md#ci-覆盖范围)对比有界的 `Prerequisite snapshot:` 诊断。
记录运行 ID、attempt 及待终结任务／步骤身份。稍后读取成功不能替代原运行的失败验收；
状态摘要仅用于诊断，不保证数据已刷新。

## 基准测试和报告

基准源码放在 `benchmarks/`，让库的使用者能够自行运行，并保持与 doctest 独立。
比较时使用相同硬件、依赖、编译配置和工作负载；基准测试期间不要同时构建或运行
其他测试。

标准流程、参数和测量定义见[测量方法](../performance/methodology_CN.md)。[IO 模型](../concepts/io-models_CN.md)
说明共享 context 和分片 context 的比较，包括原生 Asio 协程工作负载。可选报告
生成器的依赖列在 `benchmarks/requirements-report.txt`。

报告保留原始 JSON、实际命令、宿主元数据、源码版本和程序哈希。区分短时 smoke
检查与性能证据。不能根据回环吞吐推断生产容量，也不能将回调的单消息 RTT 和
协程的批次 RTT 当作相同指标比较。

## 文档和提交历史

每个文档页面都有英文和中文版本，Nimbus 页面顶部通过下拉框选择语言。修改时同步更新两个版本，
确保示例、命令、默认值、限制和状态一致。详细内容放在 `docs/`，根 README 保持
简洁。新增页面需要导航入口，以及在静态站点和 GitHub 中都可用的相对链接。

文档按概览、开始使用、核心概念、功能、使用指南、示例、参考、性能和项目组织。
概念页解释行为与约束；指南围绕一个具体任务给出步骤和验证结果；参考页列出精确的
接口与默认值；性能页保留测量条件、图表和原始数据。每个协议模块分别提供概览、
服务端、客户端、配置和性能入口，共用机制通过链接引用，避免重复维护。

编辑 `docs/` 下的 Markdown 源文件。`docs/site/` 在构建时生成双语页面、代码高亮、
搜索索引、SVG 资源、逐页纯 Markdown、`llms.txt` 和 `llms-full.txt`。
生成内容不提交到 Git。只有构建或预览文档需要 Node.js 22.12+：

```sh
cd docs/site
npm ci
npm run dev -- --host 127.0.0.1 --port 8767
npm run check
```

本地 URL 包含 `/arknet/`，与 GitHub Pages 的基础路径一致。`npm run check` 检查内容
转换、Astro 类型、构建结果、内部链接、锚点、SVG 路径和 AI 阅读入口。
语言下拉框保持当前页面；旧 Docsify hash 链接仍可跳转。
`@cloudflare/nimbus-docs` 固定版本，升级前审查 starter 的变更。

可编辑图表使用 `mermaid` 代码块。含图表的页面加载本地打包的渲染器，无需 CDN，
支持深浅色主题，较宽的图表可横向滚动。读者可展开原始代码；Markdown 导出保留
Mermaid 代码块。JavaScript 不可用或渲染失败时，代码仍可阅读。
站点构建同时生成[所打包依赖的许可证](/third-party-licenses.txt)。

连续操作使用 Nimbus 的 `<Steps>` 和 `<Step title="…">`；说明、建议、注意和错误
使用 `<Aside type="note|tip|caution|danger" title="…">`。组件标签与 Markdown 正文之间
保留空行，标题使用对应页面的语言，提示框用于需要读者留意的信息。
构建时将这些页面编译为 MDX。AI 导出将步骤转为有序列表、提示框转为带标题的引用，
保留代码和链接。

公开报告保留最终测量结果、条件和适用范围。被替代的测量及调查日志保存在原始数据归档中。

提交信息使用英文。提交前，将开发过程中的临时 commit squash 为经过审查的连贯
修改。commit 或 pull request 说明最终行为和验证结果，不保留中间调试历史。

## 第三方声明

项目使用 BSL-1.0。独立编写的 arknet 核心代码使用 OpenArkStudio 声明。保留内置
第三方文件的适用声明，包括 fecpp 和 doctest。新增内置依赖时，同步更新中英文
[第三方声明](third-party-notices_CN.md)，并按许可证要求保留许可证文件。

修改名称、格式或语言标准不会消除复制实现的许可证义务。第三方修改应独立维护，
并说明修改目的。开发和文档依赖不应成为使用 arknet 的应用运行依赖。
