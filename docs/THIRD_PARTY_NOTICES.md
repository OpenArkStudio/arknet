# Third-Party Notices

## Bundled Boost Headers

`include/arknet/bho/` contains adapted Boost headers, including Beast and its
supporting config, assertion, core, intrusive, metaprogramming, endian and
platform-detection components. BHO means Boost Header Only. Each file retains
its original authors and applicable Boost Software License notice.

- [Boost](https://www.boost.org/)
- [Boost.Beast](https://github.com/boostorg/beast)
- [Boost Software License 1.0](https://github.com/OpenArkStudio/arknet/blob/main/LICENSE)

## External Runtime Dependencies

Standalone Asio and official Boost are external dependencies, selected by the
build configuration. Their distributions contain the applicable BSL-1.0
notices. OpenSSL is an optional TLS dependency, licensed under Apache-2.0 for
3.x and the OpenSSL/SSLeay licenses for 1.1.1. Consult the actual distribution.

`ports/asio` downloads the official standalone Asio 1.38.2 distribution with a
pinned checksum; its BSL-1.0 license is installed with the dependency.

## Development and Documentation Dependencies

`tests/vendor/doctest/doctest.h` is doctest 2.5.3, copyright (c) 2016-2023
Viktor Kirilov, under the MIT License preserved in
`tests/vendor/doctest/LICENSE.txt`. It is used only by development tests.

The documentation loads [Docsify](https://github.com/docsifyjs/docsify),
[Prism](https://github.com/PrismJS/prism) and
[docsify-scroll-to-top](https://github.com/zhengxiangqi/docsify-scroll-to-top)
from a pinned CDN, all under MIT.
The optional local report generator uses
[Matplotlib](https://matplotlib.org/stable/project/license.html).
These tools do not become networking-library dependencies.
