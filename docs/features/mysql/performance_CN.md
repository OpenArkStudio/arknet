# MySQL 性能与验证

MySQL 的实测性能报告待完成。功能测试数量、协议解析吞吐量和认证成功都不能代表查询吞吐量或部署容量，目前不发布该模块的延迟、CPU 或容量数值。

## 运行基准程序

正式 C++ 程序 `arknet_mysql_benchmark` 执行只读文本或预处理 SELECT，并验证二进制回复。构建后，指定单独配置的服务端端点：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_ENABLE_SSL=ON -DARKNET_BUILD_BENCHMARKS=ON \
  -DARKNET_USE_BOOST_ASIO=ON
cmake --build build --target arknet_mysql_benchmark --config Release
build/benchmarks/arknet_mysql_benchmark --host mysql.example.com --port 3306 \
  --username application_user --ca /path/to/ca.pem --mode prepared \
  --clients 4 --window 4 --payload 1024 --warmup 0.5 --seconds 3 --rounds 3
```

密码通过 `ARKNET_MYSQL_PASSWORD` 提供，默认启用 TLS 和证书验证。[完整使用说明](../../../benchmarks/mysql_CN.md)解释模式、限额与输出指标。测试入口是编译得到的程序，不需要 Python 或 Ruby 启动器。

## 功能验证

验证环境包含隔离的 MySQL Community Server 8.4.11 实例和可控制交互的 C++ 协议服务端，覆盖 TLS 验证、密码拒绝、文本查询、二进制预处理参数、事务和会话重置。协议测试覆盖分片、报文序号、`0xffffff` 物理报文边界、字段类型、队列／结果限额、取消和回调中析构。

Boost.Asio、Standalone Asio 构建与 ASan/UBSan 用于检查正确性和内存行为，性能另行测量。可通过[完整示例](usage_CN.md)验证自己的端点连接和接口行为。

## 测量矩阵

本地查询基准至少记录以下维度，再形成性能结论：

| 维度 | 对比内容 |
| --- | --- |
| 操作 | 基准程序对比文本 SELECT 与预处理 SELECT；写入和事务需要单独负载 |
| 包体 | 1、4、16 KiB 的结果或参数值，保持行数一致 |
| 排队深度 | 每连接 1、4、16、64 条尚未完成的请求 |
| 连接数 | 固定连接数，同时报告实际 IO context 与线程数 |
| 重复测量 | 预热后，每组至少独立测量 3 轮 |
| 传输与依赖库 | 经过验证的 TLS，Boost.Asio 与 Standalone Asio |

普通连接按顺序执行命令。增大排队深度会增加尚未完成的应用请求，不会在 MySQL 线路上同时复用多条命令。事务基准需要列出每个事务包含的命令，并验证提交的数据。

## 指标与环境

报告成功操作数／秒、包含排队时间的操作 RT p50／p99（微秒）、错误数、以单核为基准的客户端 CPU 占用比例，以及客户端常驻内存峰值（字节）。查询数、返回行数和提交事务数应分别表达；测量服务端 CPU 时也需单独列出。

报告需要明确客户端与服务端硬件、系统、编译器及 Release 参数、服务端版本与配置、依赖库版本、TLS 设置、包体／行数／排队深度／连接数、预热和测量时长，以及重复轮数。客户端与服务端共用机器会影响调度；部署容量需要在目标拓扑和实际业务负载下确认，不能只依靠本地回环结果。

性能数值和图表来自已经完成的本地测量，不使用 Sanitizer 或 CI 正确性检查推断性能。

当前基准程序使用独立连接，连接池获取竞争、归还／重置和数据库恢复的开销需要单独的连接池负载。
