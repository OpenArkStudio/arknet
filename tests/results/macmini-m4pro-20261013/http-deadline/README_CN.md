# HTTP 截止时间验证

2026-10-13，Mac mini `Mac16,11`，Apple M4 Pro，14 核，48 GiB 内存，
macOS 27.0.1，Apple Clang 21.0.0，CMake 4.3.3，Standalone Asio 1.38.2，
Boost 1.90.0 和 OpenSSL 3.6.4。源码为 `e767cdf` 加本记录所在提交中的
HTTP 截止时间修复与测试。

| 构建 | 结果 |
| --- | --- |
| Standalone Asio，Release + TLS | [10 个用例、283 项断言通过](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/http-deadline/httprequest-standalone.log) |
| Boost.Asio，Release + TLS | [10 个用例、283 项断言通过](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/http-deadline/httprequest-boost.log) |
| Standalone Asio，Debug + ASan/UBSan | [10 个用例、283 项断言通过](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/http-deadline/httprequest-asan-ubsan.log) |
| Standalone Asio，Debug + TSan | [10 个用例、283 项断言通过](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/http-deadline/httprequest-tsan.log) |

已发布的 `e767cdf` 头文件在新增的响应排队测试中失败，详见
[对照结果](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261013/http-deadline/published-header-regression.log)。已就绪的 IO 回调可能先于
已到期的定时器回调执行。请求现在会在处理 DNS、连接、写入和读取结果前
检查截止时间。测试覆盖 IO 暂停后排队的完整响应与不完整响应、完成通知
恰好执行一次，以及数字地址和域名下共 64 次极短连接超时。

编译后可直接运行项目中的 C++ 测试：

```sh
build/tests/arknet_httprequest_test
```

macOS ASan 使用 `detect_leaks=0`，不代表已完成内存泄漏检查。
本记录属于功能与 sanitizer 检查，不是性能测量。RUDP 基准源码仍固定为
`5ba514e`，对应的 960 个源码文件已逐一通过保留的 SHA-256 清单校验。
