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

standalone Asio 和官方 Boost 由构建配置选择，属于外部依赖，其分发包保留适用的
BSL-1.0 声明。OpenSSL 是可选的 TLS 依赖；3.x 使用 Apache-2.0，1.1.1 使用
OpenSSL/SSLeay 许可证。以实际使用的分发包为准。

`ports/asio` 使用固定校验和下载官方 standalone Asio 1.38.2 分发包；
依赖安装时保留其 BSL-1.0 许可证。

## 开发与文档依赖

`tests/vendor/doctest/doctest.h` 使用 doctest 2.5.3，版权所有 (c) 2016-2023
Viktor Kirilov；MIT 许可证保存在 `tests/vendor/doctest/LICENSE.txt`，仅用于开发测试。

文档通过固定版本的 CDN 加载 [Docsify](https://github.com/docsifyjs/docsify)、
[Prism](https://github.com/PrismJS/prism) 和
[docsify-scroll-to-top](https://github.com/zhengxiangqi/docsify-scroll-to-top)，均使用 MIT 许可证。
可选的本地报告生成器使用
[Matplotlib](https://matplotlib.org/stable/project/license.html)。
这些工具不是网络库的依赖。
