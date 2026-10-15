[English](README.md) | [简体中文](README_CN.md)

# 使用同一基准程序对比不同库版本

这个可选的独立 CMake 项目，从一个源码目录编译正式基准程序，使用另一个目录中的 arknet 头文件。对比两个版本时，基准程序源码目录必须相同。测试单个源码目录时，仍可使用原有的 `ARKNET_BUILD_BENCHMARKS=ON` 入口。

需要 CMake 3.21+、C++20 编译器及所选库版本的依赖。下表中的版本需要 Boost 1.90+ 或 Standalone Asio 1.38+；TLS 和 MySQL 还需要 OpenSSL 1.1.1+。依赖需提前安装，本项目不下载依赖。编译和运行不需要 Python、Ruby 或 jq。

## 编译版本对照

准备处于以下精确提交的本地源码目录。共用基准程序包含报告测量时使用的 loopback 清理顺序。

| 输入 | 提交 |
| --- | --- |
| 已发布的 0.2.0 网络库 | `25fe2e5fac4e09edee597f312d58b0362b9e0681` |
| 测量中的停止流程实现 | `2aa5bc0ef7d383d8cfbc04520f3b952fa27c5fbd` |
| 共用基准程序 | `2aa5bc0ef7d383d8cfbc04520f3b952fa27c5fbd` |

替换命令中的源码及构建目录绝对路径。两个构建均选择 Boost 并启用 TLS；`comparison` 源码路径指向本文所在目录。

```sh
cmake -S /path/to/arknet/benchmarks/comparison -B /path/to/build-v020 \
  -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_LIBRARY_SOURCE=/path/to/arknet-v020 \
  -DARKNET_BENCHMARK_DRIVER_SOURCE=/path/to/arknet-2aa5bc0 \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
cmake --build /path/to/build-v020 --config Release --parallel 1

cmake -S /path/to/arknet/benchmarks/comparison -B /path/to/build-2aa5bc0 \
  -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_LIBRARY_SOURCE=/path/to/arknet-2aa5bc0 \
  -DARKNET_BENCHMARK_DRIVER_SOURCE=/path/to/arknet-2aa5bc0 \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON
cmake --build /path/to/build-2aa5bc0 --config Release --parallel 1
```

选择 Standalone Asio 时，使用独立构建目录，将依赖选项改为 `-DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include`。HTTP 和 WebSocket 测量需要 Boost。设置 `ARKNET_ENABLE_SSL=OFF` 后，不生成 MySQL 目标，也无法使用安全传输协议。

单配置生成器将可执行文件放在 `BUILD/benchmarks`。Visual Studio 等多配置生成器会再增加配置目录，例如 `BUILD/benchmarks/Release`；Windows 可执行文件带 `.exe` 后缀。

```sh
/path/to/build-v020/benchmarks/arknet_loopback_benchmark --help
/path/to/build-v020/benchmarks/arknet_coroutine_benchmark --help
/path/to/build-v020/benchmarks/arknet_redis_benchmark --help
/path/to/build-v020/benchmarks/arknet_mysql_benchmark --help
```

对第二个构建运行相同命令。`--help` 成功仅说明程序入口可运行，不会连接服务或测量性能。

## 明确测量范围

基准程序保留原有参数和逐行 JSON 输出：

- `loopback`：arknet 公开传输接口；CPU 包含同一进程中的客户端和服务端。本地 TLS 回环测试需传入 `--certs /path/to/arknet-2aa5bc0/tests/certs`，其中的证书仅供测试使用。
- `coroutine`：原生 Asio TCP 回调与协程对照，不代表 arknet 公开端点的协程性能。
- `redis`：连接已运行的隔离 Redis 兼容服务，执行二进制 ECHO。延迟覆盖一批流水线请求，CPU 仅包含客户端进程。
- `mysql`：连接已运行的隔离 MySQL 服务，执行只读文本 `SELECT` 或预处理 `SELECT ?`。CPU 仅包含客户端进程。

数据库 TLS 测试需提供受信任 CA，连接主机名必须与服务端证书匹配。MySQL 默认验证 TLS，通过 `--ca FILE` 指定 CA；Redis 需使用 `--tls --ca FILE`。密码通过环境变量 `ARKNET_MYSQL_PASSWORD` 和 `ARKNET_REDIS_PASSWORD` 提供；其他账号设置使用 `--username` 和 MySQL 的 `--database`。不要通过关闭证书验证使测试通过。连接配置参见 [MySQL 指南](../../docs/features/mysql/usage_CN.md)和 [Redis 指南](../../docs/features/redis/usage_CN.md)。

版本对比需保持编译器、依赖、TLS、包体大小、连接数、在途请求数、预热时间、测量时长及机器负载一致。两个版本逐组相邻运行，交替先后顺序，每组使用新进程重复测量。将完整命令、库与基准程序提交、源码和可执行文件哈希、环境、错误数、吞吐量、延迟、CPU 和 RSS 保存在源码目录外。CPU 100% 表示占满一个核心。原生 Asio 对照、公开传输接口和数据库测试的吞吐量不能混成一个数值。

这个构建入口不会启动服务、执行测量矩阵、汇总结果或下载历史版本。测量范围和结果参见[数据库版本对比](../../docs/performance/database-comparison_CN.md)、[传输协议版本对比](../../docs/performance/transport-comparison_CN.md)和 [Asio 执行方式对照](../../docs/performance/asio-execution-control_CN.md)。
