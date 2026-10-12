# 第三方声明

## 内置 Boost 头文件

`include/arknet/bho/` 包含经过适配的 Boost 头文件，包括 Beast 及其使用的
config、assertion、core、intrusive、metaprogramming、endian 和平台检测组件。
BHO 指 Boost Header Only。各文件保留原作者和适用的 Boost Software License
声明。

- [Boost](https://www.boost.org/)
- [Boost.Beast](https://github.com/boostorg/beast)
- [Boost Software License 1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE)

## 外部运行时依赖

Standalone Asio 和官方 Boost 由构建配置选择，属于外部依赖，其分发包保留适用的
BSL-1.0 声明。OpenSSL 是可选的 TLS 依赖；3.x 使用 Apache-2.0，1.1.1 使用
OpenSSL/SSLeay 许可证。以实际使用的分发包为准。

`ports/asio` 使用固定校验和下载官方 standalone Asio 1.38.2 分发包；
依赖安装时保留其 BSL-1.0 许可证。

## 开发与文档依赖

`tests/vendor/doctest/doctest.h` 使用 doctest 2.5.3，版权所有 (c) 2016-2023
Viktor Kirilov；MIT 许可证保存在 `tests/vendor/doctest/LICENSE.txt`，仅用于开发测试。

`tests/vendor/kcp/ikcp.c` 与 `ikcp.h` 是固定提交
`b1a7a2101dcbb96017681a500d6b82bbe5a88766` 的官方 KCP 实现，
copyright (c) 2017 Lin Wei (skywind3000)，MIT 许可证保存在 `tests/vendor/kcp/LICENSE`。
它们仅作为独立互通测试对照，不属于运行时依赖，也不会安装为公开头文件。
C++ ARQ 核心由 OpenArkStudio 维护，使用 BSL-1.0。

文档使用 [Cloudflare Nimbus](https://github.com/cloudflare/nimbus) 0.17.0
和 0.7.11 starter，copyright (c) 2025 Cloudflare, Inc.，采用 MIT。
starter 的许可证保存在 [docs/site/LICENSE](https://github.com/OpenArkStudio/arknet/blob/main/docs/site/LICENSE)。
Astro、Tailwind CSS、Pagefind 和 Markdown 工具仅用于文档构建；各自许可证随锁定版本的 npm 包分发。
可选的本地报告生成器使用
[Matplotlib](https://matplotlib.org/stable/project/license.html)。
这些工具不是网络库的依赖。
