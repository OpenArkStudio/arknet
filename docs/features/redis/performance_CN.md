# Redis 性能

目前没有发布 Redis 的实测性能报告。仓库提供独立 C++ 基准程序；性能结论需建立在完整的本地测试矩阵，以及通过的正确性和关闭流程检查之上。

## 复现

单独启动 Redis 服务，使用 Release 模式构建基准程序，并指定服务端端点：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build build --target arknet_redis_benchmark --config Release

build/benchmarks/arknet_redis_benchmark --host 127.0.0.1 --port 6379 \
  --clients 4 --payload 1024 --pipeline 16 --window 1 \
  --warmup 0.5 --seconds 3 --rounds 3
```

认证使用 `ARKNET_REDIS_PASSWORD`，用户名通过可选的 `--username` 指定。TLS 使用对应端口，并添加 `--tls --ca /path/to/ca.pem`。[完整使用说明](../../../benchmarks/redis_CN.md)包含参数、限额与输出解释，无需 Python 或 Ruby 作为启动入口。

## 负载与对比维度

程序发送二进制 `ECHO`，逐条验证回复类型、数量和内容。每条连接使用一个 IO context 和线程。至少比较以下维度：

| 维度 | 取值 |
| --- | --- |
| 包体 | 1、4、16 KiB |
| 每批命令数 | 1、4、16、64 条 |
| 每连接同时在途批次 | 1、4 批 |
| 重复测量 | 每组至少 3 轮 |
| 依赖库与传输方式 | Boost.Asio／Standalone Asio，TCP／TLS |

同组比较保持连接数、服务端配置和机器条件一致，记录客户端与服务端的硬件、系统、编译器与构建模式，以及 Redis、Asio、OpenSSL 版本、预热和测量时长。客户端与 Redis 服务端共用机器会影响 CPU 调度，不能据此推断跨机器部署容量。

## 指标

| 指标 | 单位与解释 |
| --- | --- |
| 命令吞吐量 | 每秒验证成功的命令数 |
| 整批 RT p50／p99 | 从提交到整批回复完成的微秒数，包含客户端排队，不是单条命令延迟 |
| 客户端 CPU | 以单核为基准的占用比例；100% 表示占满一个核心，不包含 Redis 服务进程 |
| 内存峰值 | 客户端进程生命周期内的常驻内存峰值，单位为字节，包含预热及之前轮次 |

全部轮次成功后，每轮正式测量输出一行 JSON；连接、预热或回复验证失败时，以失败状态退出且不输出 JSON。延迟采样有全局上限，并按完成批次数加权；短时间内的少量样本不足以描述罕见延迟。此程序覆盖回调连接接口，不代表协程调度或连接池竞争的性能。
