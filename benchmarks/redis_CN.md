# Redis 性能测试

`arknet_redis_benchmark` 是可独立编译运行的 C++ 程序，用于测试原生 Redis 客户端，Redis 服务端需要单独启动。程序发送二进制 `ECHO`，逐条验证回复类型、数量和内容。包体达到 256 字节时会包含零字节及所有字节值，不修改 Redis 键。

[本地性能报告](../docs/features/redis/performance_CN.md)记录 96 组、每组 3 轮的矩阵及实测源码哈希，服务端使用支持 Redis 协议的 Valkey 8.0.11。[配置指南](../docs/features/redis/configuration_CN.md#服务端-tls-重试问题)区分该测量与 Redis 7.4.2 的互通验证及其 TLS 重试问题。

## 编译与运行

```sh
cmake -S . -B build/redis-benchmark -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_BUILD_TESTS=OFF -DARKNET_BUILD_EXAMPLES=OFF \
  -DARKNET_BUILD_BENCHMARKS=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DARKNET_ENABLE_SSL=ON
cmake --build build/redis-benchmark --target arknet_redis_benchmark --config Release

./build/redis-benchmark/benchmarks/arknet_redis_benchmark \
  --host 127.0.0.1 --port 6379 --clients 4 \
  --pipeline 16 --window 1 --payload 1024 \
  --warmup 0.5 --seconds 3 --rounds 3
```

使用多配置生成器时，可执行文件位于 `benchmarks/Release/`。使用 Standalone Asio 时，将 `ARKNET_USE_BOOST_ASIO` 设为 `OFF`；若默认路径中没有对应头文件，还需设置 `ARKNET_ASIO_INCLUDE_DIR`。测试入口不依赖 Python 或 Ruby。

需要认证时，通过环境变量 `ARKNET_REDIS_PASSWORD` 提供密码；程序不接受密码命令行参数，也不输出密码。ACL 用户额外指定 `--username USER`。TLS 连接使用对应端口，并添加 `--tls --ca /path/to/ca.pem`，证书和主机名验证始终启用。省略 `--ca` 时使用客户端默认的信任配置。

## 测试维度

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `--clients` | `4` | 连接数；每条连接使用网络库管理的独立 IO context 和线程，最多 128 条 |
| `--pipeline` | `16` | 每批同时提交的命令数，最多 1024 条 |
| `--window` | `1` | 每条连接同时在途的批次数，最多 64 批 |
| `--payload` | `1024` | 每条命令回显的字节数，支持 0 到 1 MiB |
| `--warmup` | `0.25` | 正式测量前的预热秒数，同样验证回复；0 表示跳过 |
| `--seconds` | `1` | 每轮正式测量的目标秒数 |
| `--rounds` | `3` | 独立测量轮数，最多 100 轮 |

每条连接最多有 `pipeline × window` 条在途命令。包体加上保守估算的每条命令开销，需要满足每条连接 64 MiB 的上限，超出时会在连接前拒绝运行。每轮结束前等待最后一批完成，因此 `elapsed_seconds` 可能略大于 `--seconds`。

建议交叉比较包体大小（例如 1024、4096、16384 字节）、批量大小（1、4、16、64 条）和在途批次（1、4 批），每组至少重复 3 轮。除本次比较的维度外，保持客户端与服务端机器、Redis 配置、编译器、依赖库版本、连接数和传输方式一致。使用 Release 构建，在空闲机器上测试；不要将 Sanitizer 构建或 CI 机器结果混入本机性能对比。

## 输出指标

预热和全部正式测量均成功后，程序才向标准输出写入每轮一行的 JSON，诊断信息写入标准错误。连接、预热、内容验证或测量失败时，程序以非零状态退出且不输出 JSON；即使之前已有成功轮次，也不会留下部分结果。

| 指标 | 含义 |
| --- | --- |
| `commands_per_second` | 验证成功的命令数除以实际测量时长 |
| `batch_roundtrip_p50_us`、`batch_roundtrip_p99_us` | 整批流水线从提交到全部回复完成的耗时，单位为微秒，包含客户端排队；**不是单条命令的延迟** |
| `client_cpu_percent_of_one_core` | 客户端进程 CPU 时间除以实际时长，再乘以 100；占满一个核心为 100%，多线程可能超过 100% |
| `process_peak_rss_bytes` | 客户端进程启动以来的常驻内存峰值，包含预热及之前的轮次，不是本轮新增内存 |
| `successful_commands`、`failed_commands` | 整批验证后的命令计数；批次失败时，该批命令全部计为失败 |
| `latency_sample_count` | 所有并发请求链保留的批次耗时样本数，合计不超过 65536 |

CPU 和内存指标不包含外部 Redis 服务端；平台不支持的指标输出 `null`。每条并发请求链采用有上限的蓄水池采样；合并分位数时，按该链的成功批次数与样本数之比加权。发生采样时，分位数为估计值。解读尾延迟前应检查样本量，短时间内完成的少量批次不足以反映罕见延迟。

此程序测量回调接口，以及每条连接一个 IO context、一个线程的配置，不代表协程接口、共享 IO context、连接池或实际部署容量的性能结论。

## 生成报告

可选的 Node 工具 `benchmarks/report_database_clients.mjs` 将完整 JSONL 结果转换为双语 Markdown 与 SVG 图表。只有生成报告需要 Node，基准程序本身不需要。原始数据保存在仓库外，另行提供测试元数据、预期组合数和实测源码哈希；字段见 `--help`。

[测量数据包](https://github.com/OpenArkStudio/arknet/releases/download/v0.2.0/arknet-0.2.0-benchmark-data.tar.gz)包含报告对应的 JSONL 轮次、元数据、命令与源码哈希，不包含可执行文件。将其解压到仓库外，例如 `/tmp`，即可渲染已记录的测量结果：

```sh
node benchmarks/report_database_clients.mjs \
  --input /tmp/arknet-0.2.0-benchmark-data/redis/rows \
  --metadata /tmp/arknet-0.2.0-benchmark-data/redis/metadata.json \
  --out-dir /tmp/database-report
node --test benchmarks/report_database_clients.test.mjs
```

输出遵循文档目录结构：`features/redis/performance*.md` 和 `performance/assets/redis-*.svg`。包含失败结果或不完整矩阵时，会在写入报告前拒绝处理。柱为各轮中位数，误差线为最小值到最大值；RT p50／p99 汇总的是每轮分位数，不是合并原始延迟后重新计算的分位数。
