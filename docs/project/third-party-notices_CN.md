# 第三方声明

| 依赖 | 用途 | 上游许可证 |
| --- | --- | --- |
| [fecpp · d7e9b41](https://github.com/randombit/fecpp/tree/d7e9b41eaaab3c605be36f282465210d17bc432f) | 可移植 C++20 header-only Reed–Solomon 适配；未修改的测试对照 | [BSD-2-Clause](https://github.com/randombit/fecpp/blob/d7e9b41eaaab3c605be36f282465210d17bc432f/license.txt) |
| Standalone Asio／Boost | 由构建配置选择的外部依赖 | [Asio BSL-1.0](https://github.com/chriskohlhoff/asio/blob/asio-1-38-2/LICENSE_1_0.txt)、[Boost BSL-1.0](https://www.boost.org/LICENSE_1_0.txt) |
| OpenSSL | 可选 TLS | [3.x Apache-2.0](https://github.com/openssl/openssl/blob/openssl-3.0.0/LICENSE.txt)、[1.1.1 OpenSSL/SSLeay](https://github.com/openssl/openssl/blob/OpenSSL_1_1_1-stable/LICENSE) |
| doctest 2.5.3 | 仅开发测试 | [MIT](https://github.com/doctest/doctest/blob/v2.5.3/LICENSE.txt) |
| [KCP · b1a7a21](https://github.com/skywind3000/kcp/tree/b1a7a2101dcbb96017681a500d6b82bbe5a88766) | 仅用官方 `ikcp.c`／`ikcp.h` 作互通对照；不是运行时依赖，不安装为头文件 | [MIT](https://github.com/skywind3000/kcp/blob/b1a7a2101dcbb96017681a500d6b82bbe5a88766/LICENSE) |
| Cloudflare Nimbus 0.17.0／starter 0.7.11 | 文档 | [MIT](https://github.com/cloudflare/nimbus/blob/main/LICENSE) |
| Astro／Tailwind CSS／Pagefind／Markdown 工具 | 文档构建，许可证随锁定版本的 npm 包分发 | [Astro](https://github.com/withastro/astro/blob/main/LICENSE)、[Tailwind CSS](https://github.com/tailwindlabs/tailwindcss/blob/main/LICENSE)、[Pagefind](https://github.com/Pagefind/pagefind/blob/main/LICENSE)、[锁定包](https://github.com/OpenArkStudio/arknet/blob/main/docs/site/package-lock.json) |
| Matplotlib | 可选本地报告生成器 | [许可证](https://matplotlib.org/stable/project/license.html) |

## 保留声明与分发

- fecpp：copyright (c) 1997–1998 Luigi Rizzo、2009–2010 Jack Lloyd、2011 Billy Brumley。`include/arknet/thirdparty/fecpp.LICENSE` 与 `fecpp.hpp` 一起安装；`tests/vendor/fecpp/` 保留固定提交的原始源码和许可证，用于独立校验编码与恢复结果。
- doctest：copyright (c) 2016–2023 Viktor Kirilov，保留在 `tests/vendor/doctest/LICENSE.txt`。KCP：copyright (c) 2017 Lin Wei (skywind3000)，保留在 `tests/vendor/kcp/LICENSE`。C++ ARQ 核心由 OpenArkStudio 维护，使用 BSL-1.0。
- Nimbus／starter：copyright (c) 2025 Cloudflare, Inc.，starter 许可证保留在 [docs/site/LICENSE](https://github.com/OpenArkStudio/arknet/blob/main/docs/site/LICENSE)。

`ports/asio` 以固定校验和下载官方 Standalone Asio 1.38.2，并安装其 BSL-1.0 许可证。外部分发包提供适用的许可证／声明，以实际分发包为准。文档／报告工具不是网络库依赖。
