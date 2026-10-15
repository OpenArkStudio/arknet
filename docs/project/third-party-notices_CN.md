# 第三方声明

## 内置 Reed–Solomon 编码引擎

`include/arknet/thirdparty/fecpp.hpp` 将 [fecpp](https://github.com/randombit/fecpp)
的固定提交 `d7e9b41eaaab3c605be36f282465210d17bc432f` 适配为 C++20 header-only 实现。
保留 copyright (c) 1997–1998 Luigi Rizzo、2009–2010 Jack Lloyd 和 2011 Billy Brumley。
BSD-2-Clause 许可证与头文件一起安装，路径为 `include/arknet/thirdparty/fecpp.LICENSE`。
`tests/vendor/fecpp/` 保留相同提交的原始源码和许可证，用于独立校验编码与恢复结果。

## 外部 Asio 与 TLS 依赖

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
