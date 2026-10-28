# Third-Party Notices

| Dependency | Use | Upstream license |
| --- | --- | --- |
| [fecpp · d7e9b41](https://github.com/randombit/fecpp/tree/d7e9b41eaaab3c605be36f282465210d17bc432f) | Portable C++20 header-only Reed–Solomon adaptation; unchanged test reference | [BSD-2-Clause](https://github.com/randombit/fecpp/blob/d7e9b41eaaab3c605be36f282465210d17bc432f/license.txt) |
| Standalone Asio / Boost | External dependencies selected by build configuration | [Asio BSL-1.0](https://github.com/chriskohlhoff/asio/blob/asio-1-38-2/LICENSE_1_0.txt), [Boost BSL-1.0](https://www.boost.org/LICENSE_1_0.txt) |
| OpenSSL | Optional TLS | [3.x Apache-2.0](https://github.com/openssl/openssl/blob/openssl-3.0.0/LICENSE.txt), [1.1.1 OpenSSL/SSLeay](https://github.com/openssl/openssl/blob/OpenSSL_1_1_1-stable/LICENSE) |
| doctest 2.5.3 | Development tests only | [MIT](https://github.com/doctest/doctest/blob/v2.5.3/LICENSE.txt) |
| [KCP · b1a7a21](https://github.com/skywind3000/kcp/tree/b1a7a2101dcbb96017681a500d6b82bbe5a88766) | Canonical `ikcp.c`/`ikcp.h` interoperability reference only; not a runtime dependency or installed header | [MIT](https://github.com/skywind3000/kcp/blob/b1a7a2101dcbb96017681a500d6b82bbe5a88766/LICENSE) |
| Cloudflare Nimbus 0.17.0 / starter 0.7.11 | Documentation | [MIT](https://github.com/cloudflare/nimbus/blob/main/LICENSE) |
| Astro / Tailwind CSS / Pagefind / Markdown tooling | Documentation builds; licenses accompany locked npm packages | [Astro](https://github.com/withastro/astro/blob/main/LICENSE), [Tailwind CSS](https://github.com/tailwindlabs/tailwindcss/blob/main/LICENSE), [Pagefind](https://github.com/Pagefind/pagefind/blob/main/LICENSE), [locked packages](https://github.com/OpenArkStudio/arknet/blob/main/docs/site/package-lock.json) |
| Matplotlib | Optional local report generator | [License](https://matplotlib.org/stable/project/license.html) |

## Retained notices and distribution

- fecpp: copyright (c) 1997–1998 Luigi Rizzo, 2009–2010 Jack Lloyd, 2011 Billy Brumley. `include/arknet/thirdparty/fecpp.LICENSE` is installed beside `fecpp.hpp`; `tests/vendor/fecpp/` retains the pinned original source and license for independent parity/recovery checks.
- doctest: copyright (c) 2016–2023 Viktor Kirilov; retained in `tests/vendor/doctest/LICENSE.txt`. KCP: copyright (c) 2017 Lin Wei (skywind3000); retained in `tests/vendor/kcp/LICENSE`. OpenArkStudio maintains the C++ ARQ core under BSL-1.0.
- Nimbus/starter: copyright (c) 2025 Cloudflare, Inc.; starter license retained in [docs/site/LICENSE](https://github.com/OpenArkStudio/arknet/blob/main/docs/site/LICENSE).

`ports/asio` downloads official Standalone Asio 1.38.2 with a pinned checksum and installs its BSL-1.0 license. External distributions supply their applicable licenses/notices; consult the actual distribution. Documentation/report tools are not networking-library dependencies.
