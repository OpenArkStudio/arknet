# 数据库客户端验证

验证日期为 2026-10-15，范围为包含本记录的源码，CMake 版本元数据为 `0.2.0`。
验证本身不发布版本。

## 环境

| 项目 | 配置 |
| --- | --- |
| 机器 | Mac mini `Mac16,11`，Apple M4 Pro，14 核：10 个性能核心、4 个能效核心 |
| 内存／系统 | 48 GiB／macOS 27.0.1（26A434） |
| Release 编译器 | Apple Clang 21.0.0 |
| Sanitizer 编译器 | Homebrew LLVM 22.1.7，arm64-apple-darwin27.0.0 |
| 构建／测试框架 | CMake 4.3.3／doctest 2.5.3 |
| 依赖库 | Boost 1.90.0、Boost.Asio 1.38.0、Standalone Asio 1.38.2、OpenSSL 3.6.4 |
| 服务端 | 官方 MySQL Community 8.4.11 ARM64 与 Redis 7.4.2，仅监听回环地址 |

## 结果

完整 Release 检查使用整合源码，启用 TLS 和真实数据库用例。
以下总数覆盖整个 `arknet_check`：协议及编解码、进程检查、安装包使用和基准冒烟。
这些结果不作为性能测量。

| 依赖方案 | 检查数 | doctest 用例数 | 断言数 | 失败／跳过 |
| --- | ---: | ---: | ---: | --- |
| Boost.Asio | 75 | 374 | 1,952,026 | 0／0 |
| Standalone Asio | 62 | 314 | 976,980 | 0／0 |

HTTP 和 WebSocket 依赖 Boost.Beast，两种配置的协议覆盖因此不同；Standalone Asio
还有独立的线程存储配置检查。检查数按执行的程序计数，用例与断言仅统计 doctest 输出。
进程退出、全局所有权、跨编译单元以及监听地址并发停止检查，与下面的数据库测试分开统计。

| 检测工具／依赖方案 | 目标 | 用例数 | 断言数 | 失败／跳过 |
| --- | --- | ---: | ---: | --- |
| TSan／Boost.Asio | MySQL connection | 34 | 1,488 | 0／0 |
| TSan／Boost.Asio | MySQL pool | 17 | 411 | 0／0 |
| TSan／Standalone Asio | MySQL connection | 34 | 1,488 | 0／0 |
| TSan／Standalone Asio | MySQL pool | 17 | 411 | 0／0 |
| ASan+UBSan／Boost.Asio | MySQL connection | 34 | 1,488 | 0／0 |
| ASan+UBSan／Boost.Asio | MySQL pool | 17 | 411 | 0／0 |
| TSan／Boost.Asio | Redis client | 21 | 183 | 0／0 |
| TSan／Boost.Asio | Redis pool | 14 | 133 | 0／0 |
| ASan+UBSan／Standalone Asio | Redis client | 21 | 183 | 0／0 |
| ASan+UBSan／Standalone Asio | Redis pool | 14 | 133 | 0／0 |

Boost.Asio 的四个 TSan 目标合计 **86 个用例、2,215 项断言**。
MySQL 的 Standalone Asio TSan 与 Boost.Asio ASan+UBSan 分别通过
**51 个用例、1,899 项断言**；Standalone Asio 的 Redis ASan+UBSan 通过
**35 个用例、316 项断言**。MySQL 与 Redis 分别执行，对应下文记录的产品头文件，
并非同时运行的服务负载测试。没有 Sanitizer 诊断，也没有使用抑制规则。
macOS ASan 执行关闭了泄漏检测，不能代表 Linux LeakSanitizer 检查。

## 检查范围

| 范围 | 验证内容 |
| --- | --- |
| MySQL 连接 | TLS 验证与错误身份／密码拒绝；握手／认证分片及序号校验；native 与 caching-SHA2 认证；查询、预处理／二进制结果、事务、结果上限及物理报文边界续包 |
| MySQL 取消／连接池 | 投递前预留数量与字节配额；包含排队时间的截止时间；已发送请求中断后报告结果不确定且不重放；FIFO 独占租约、回调排空、实际重置、恢复配置的数据库及拒绝失效语句句柄 |
| MySQL 投递生命周期 | 异步投递返回前保留运行时持有者，命令定时器先于运行时持有者释放，覆盖正常、取消、无效连接参数及命令拒绝回调中的连接销毁 |
| Redis 客户端 | RESP2/RESP3 认证、真实 TCP/TLS/mTLS、二进制流水线、回复顺序、同步准入、取消后保留回复位置与配额，以及有界回复排空 |
| Redis 连接池 | 独占租约、获取取消／截止时间、回调计数、RESET 后恢复协议／认证／数据库、失败连接丢弃与替换 |
| 边界回归 | 无法表示的超时；14 字节预算可以编码 RESP2 PING，而 RESET 需要 15 字节；本地拒绝 REPLCONF，不发送命令、不改变后续回复关联 |

RESET 回归要求首次获取与 PING 成功，归还时安全丢弃连接，并能重新获取连接、完成 PING。
REPLCONF 回归检查 IO 线程中的拒绝回调恰好执行一次、不占用后续配额，以及下一条 ECHO 的准确结果。
正式 C++ 用例分别位于 [redis_pool.cpp](../../../redis_pool.cpp)
和 [redis_client.cpp](../../../redis_client.cpp)。

[mysql_connection.cpp](../../../mysql_connection.cpp) 独立覆盖无效连接参数和命令拒绝回调中的连接销毁。
聚合准入用例使用两条编码后各至少 33 字节的查询，合计超过 64 字节预算，
不依赖小字符串优化（SSO）或特定字符串容量。MySQL 编解码测试显式包含 `<ostream>`，
满足 MSVC 断言诊断的头文件要求。

[2026-10-13 RUDP/FEC 记录](../../macmini-m4pro-20261013/rudp-fec/README_CN.md)
保留互通和 Sanitizer 专项证据。其中 55 项检查、219 个用例的完整执行总数对应当时的源码，
不代表本次整合。

## 复现

在仓库根目录执行。测试入口是独立 C++ 可执行文件，不需要 Python 或 Ruby。
使用隔离且可丢弃的服务，不要连接生产数据库：MySQL 用例会创建、删除测试表，
Redis 用例会临时修改测试键和一个 ACL 用户。不要让多份测试同时访问同一测试服务。

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON \
  -DARKNET_BUILD_TESTS=ON -DARKNET_BUILD_BENCHMARKS=ON
cmake --build build-release --parallel 2
```

启动 MySQL，使用 `tests/certs` 中的测试 CA 与服务端证书，创建可丢弃的 `arknet_tests`
数据库，并为测试账号授予该数据库内创建、删除表的权限。本次 MySQL 监听
`127.0.0.1:34306`。仓库证书仅用于测试身份。

```sh
export ARKNET_MYSQL_TEST_HOST=127.0.0.1
export ARKNET_MYSQL_TEST_PORT=34306
export ARKNET_MYSQL_TEST_USER=arknet_test
export ARKNET_MYSQL_TEST_PASSWORD=arknet-tests-only
export ARKNET_MYSQL_TEST_DATABASE=arknet_tests
export ARKNET_MYSQL_TEST_CA="$PWD/tests/certs/ca.pem"
```

启动支持 TLS 的 Redis，使用相同的 CA／服务端证书，以及仅用于测试的密码
`arknet-tests-only`。提供 TCP、无需客户端证书的 TLS，以及要求客户端证书的独立 TLS
监听端口。关闭持久化，仅绑定回环地址。记录中的 Redis Sanitizer 执行使用
`26579`、`26580`、`26581`。

```sh
export ARKNET_REDIS_TEST_PORT=26579
export ARKNET_REDIS_TEST_TLS_PORT=26580
export ARKNET_REDIS_TEST_MTLS_PORT=26581
export ARKNET_REDIS_TEST_CA="$PWD/tests/certs/ca.pem"
export ARKNET_REDIS_TEST_CERT="$PWD/tests/certs/client.pem"
export ARKNET_REDIS_TEST_KEY="$PWD/tests/certs/client-key.pem"
cmake --build build-release --target arknet_check
```

缺少服务环境变量时，真实服务用例会跳过。完整复现应确认没有跳过的用例。
日志位于 `build-release/tests/logs/`。测试完成后停止自行启动的服务。

只执行数据库 TSan 目标时，使用 LLVM 22 和相同的服务环境：

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CXX_COMPILER=/path/to/llvm-22/bin/clang++ \
  -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer -include cfloat' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON \
  -DARKNET_BUILD_TESTS=ON -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=OFF
cmake --build build-tsan --parallel 2 --target \
  arknet_mysql_connection_test arknet_mysql_pool_test \
  arknet_redis_client_test arknet_redis_pool_test
export TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1
build-tsan/tests/arknet_mysql_connection_test
build-tsan/tests/arknet_mysql_pool_test
build-tsan/tests/arknet_redis_client_test
build-tsan/tests/arknet_redis_pool_test
```

RelWithDebInfo 的实际参数包含 `-O2 -g -DNDEBUG -std=gnu++20`。额外的 `-include cfloat`
用于处理 LLVM 22／macOS SDK／doctest 的 `DBL_EPSILON` 头文件顺序问题，并非 Sanitizer
抑制。MySQL Boost.Asio 与 Redis Standalone Asio 的 ASan+UBSan 构建使用
`-fsanitize=address,undefined` 编译与链接，实际优化级别为 `-O2`。
两者运行参数均包含 `ASAN_OPTIONS=detect_leaks=0` 和 `UBSAN_OPTIONS=halt_on_error=1`，
MySQL 另外启用 `print_stacktrace=1`。
使用 Standalone Asio TSan 时，在独立构建目录设置 `ARKNET_USE_BOOST_ASIO=OFF`
和 `ARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`，再以相同服务环境运行两个 MySQL 目标。
不同 Sanitizer 与 Release 性能测量使用独立构建目录。

## 产品头文件 SHA-256

| 头文件 | SHA-256 |
| --- | --- |
| `mysql/connection.hpp` | `8e88c222abe98b7cc1e5e1aa27053cef8c080ea5c8b9ef01852372e4dca7d1c0` |
| `mysql/pool.hpp` | `44e56ac33457dad19dcd80fccef10cd657acddb35a10ca9d7035435aeeb419bb` |
| `redis/client.hpp` | `952d403284598bbcaeece7373459190bb2773b7c60d46bb95eb66e8b49f1f849` |
| `redis/pool.hpp` | `8cf76f81a872efb477861541580f57a0bb97cb2174cd77995308a6353ae63a47` |

本记录只保存结果、环境、命令和源码标识，不纳入原始 JSON 或大量本机日志。
本机正确性与回环吞吐量不能证明 10 万～20 万 PCU、跨主机行为或生产时长耐久。
相关要求见[生产验收](../../../../docs/project/production-readiness_CN.md)
和[性能方法](../../../../docs/performance/methodology_CN.md)。
