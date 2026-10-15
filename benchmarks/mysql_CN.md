# MySQL 客户端基准测试

编译并运行独立 C++ 程序，连接待测 MySQL 服务。程序只查询常量二进制值，不建表，也不写入业务数据。

```sh
cmake -S . -B build-mysql -DCMAKE_BUILD_TYPE=Release \
  -DARKNET_USE_BOOST_ASIO=ON -DARKNET_ENABLE_SSL=ON \
  -DARKNET_BUILD_BENCHMARKS=ON
cmake --build build-mysql --target arknet_mysql_benchmark --parallel 3
export ARKNET_MYSQL_PASSWORD='your-test-password'
./build-mysql/benchmarks/arknet_mysql_benchmark \
  --host db.example.com --port 3306 --username benchmark --database benchmark \
  --ca /path/to/ca.pem --mode prepared --clients 4 --window 16 \
  --payload 4096 --rounds 3 --seconds 5 --warmup 1
unset ARKNET_MYSQL_PASSWORD
```

只有明确需要明文测试时，才用 `--plain` 替换 `--ca`。程序仍需 OpenSSL 计算认证摘要；需要完整密码交换的认证流程可能拒绝明文连接。

使用 Standalone Asio 时，将 `ARKNET_USE_BOOST_ASIO` 设为 `OFF`，并指定 `ARKNET_ASIO_INCLUDE_DIR`。
运行程序不需要 Python 或 Ruby。批量测试可用 shell 循环，原始 JSON 输出放在源码目录外。

## 负载与维度

| 参数 | 含义 | 默认值 |
| --- | --- | --- |
| `--mode text` | 使用 `COM_QUERY` 发送 `SELECT X'…'`，每个包体字节编码为两个十六进制字符 | — |
| `--mode prepared` | 每个连接预处理一次 `SELECT ?`，随后绑定二进制字符串执行 | `prepared` |
| `--clients` | 并发连接数，每个连接使用一个 IO context 和一个工作线程 | 4 |
| `--window` | 每连接未完成查询数，包含执行中的命令及 FIFO 队列 | 1 |
| `--payload` | 查询结果的字节数，每条响应均逐字节校验 | 1024 |
| `--rounds` | 预热后，在相同连接上重复测量的次数 | 3 |
| `--seconds` | 每轮目标时长，随后等待未完成查询结束 | 1 |
| `--warmup` | 正式测量前的预热时长 | 0.25 |

MySQL 每个连接同时执行一条命令。增大 `--window` 会增加客户端排队，不会让一个 socket 并发执行多条语句。
两种模式均使用回调接口，不包含协程和连接池。文本查询与预处理查询的请求表示不同，对比结果反映完整执行路径的差异，不能直接归因为编解码开销。

## 输出指标

程序缓存所有轮次的记录，全部成功后才按每轮一行的方式输出 JSON。连接、预热、响应校验或正式测量失败时，程序返回非零退出码。
失败运行的全部输出均应丢弃，包括失败前已完成的轮次。

| 指标 | 含义 |
| --- | --- |
| `queries_per_second` | 校验成功的查询数除以实测时长，包含最后一批查询的完成时间 |
| `query_p50_us`、`query_p99_us` | 从提交到完成的延迟，包含客户端排队；使用各请求链加权蓄水池采样估算 |
| `client_cpu_percent` | 客户端进程 CPU 占用；占满一个核为 100%，多核运行可超过 100% |
| `process_peak_rss_bytes` | 进程整个运行期间的驻留内存峰值，不是本轮分配量，也不是单连接内存 |
| `failed_queries` | 失败或响应不匹配的查询数；有效运行应为零 |

所有请求链合计最多保留约 65,536 条延迟样本。连接建立、认证和语句预处理在预热前完成，不计入测量。
CPU 指标不包含服务端。发布结果时应记录服务端版本和配置、两端机器、编译器、依赖版本、TLS 设置及网络环境。
本机回环测试不能直接证明跨机器部署容量。

## 生成报告

可选的 Node 工具 `benchmarks/report_database_clients.mjs` 将完整 JSONL 矩阵转换为双语 Markdown 与 SVG 图表。编译得到的基准程序不需要 Node。原始数据保存在仓库外，另行提供测试元数据、预期组合数和实测源码哈希；字段见 `--help`。

```sh
node benchmarks/report_database_clients.mjs --input /tmp/mysql-rounds \
  --metadata /tmp/mysql-measurement.json --out-dir /tmp/database-report
node --test benchmarks/report_database_clients.test.mjs
```

输出遵循文档目录结构：`features/mysql/performance*.md` 和 `performance/assets/mysql-*.svg`。失败、不完整或重复的测量会在输出前被拒绝。柱为各轮中位数，误差线为最小值到最大值；RT 汇总的是每轮分位数的中位数。
