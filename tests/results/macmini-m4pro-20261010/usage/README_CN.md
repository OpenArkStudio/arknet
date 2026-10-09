# Usage 示例验证

[English](README.md) | [中文](README_CN.md)

2026-10-10 对 TCP、UDP、WebSocket、TLS 和 HTTP Usage 页的代码进行了抽取和验证。
中英文 C++ 代码块逐字一致。每页的两个完整程序，以及文档说明的 HTTPS、WSS
等价程序，共构成 14 个可执行程序。

| 后端 | 依赖版本 | 编译通过的程序 | 通过的客户端与服务端组合 |
| --- | --- | ---: | ---: |
| Standalone | Asio 1.38.2 | 14 | 7 |
| Boost | Boost 1.90.0 | 14 | 7 |

两种配置均使用 AppleClang 21.0.0、OpenSSL 3.6.4、Release 和 TLS ON。
客户端验证回显内容；WebSocket 还验证二进制消息类型，HTTP 还验证状态码 200。
全部 28 个进程返回零。服务端收到 Enter 后正常退出，由运行器回收；没有超时
或强制终止。本记录仅覆盖 macOS，不代表 Linux 或 Windows 验证结果。

`standalone/` 和 `boost/` 保留配置与构建日志、各进程的标准输出和标准错误，以及
`summary.json`。`prepare.py` 和 `run.py` 是采集这些历史记录时使用的归档脚本，
不作为当前测试入口。当前功能测试和基准直接编译运行 C++ 程序，批量测量使用
`scripts/benchmark_matrix.sh`；参见[测试与测量方法](../../../../docs/testing_CN.md)。

## 历史采集命令

在仓库根目录执行。以下命令使用验证机器上的依赖路径；在其他机器上需要替换路径。
两种后端的运行检查使用相同监听端口，应按顺序执行。

```sh
python3 -B tests/results/macmini-m4pro-20261010/usage/prepare.py \
  --arknet . --output /private/tmp/arknet-usage-check/project

cmake -S /private/tmp/arknet-usage-check/project \
  -B /private/tmp/arknet-usage-check/build-standalone -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
  -DARKNET_ENABLE_SSL=ON -DARKNET_USE_BOOST_ASIO=OFF \
  -DARKNET_ASIO_INCLUDE_DIR=/private/tmp/arknet-asio-1.38.2/include \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
  > /private/tmp/arknet-usage-check/standalone-configure.log 2>&1
cmake --build /private/tmp/arknet-usage-check/build-standalone --parallel 3 \
  > /private/tmp/arknet-usage-check/standalone-build.log 2>&1
python3 -B tests/results/macmini-m4pro-20261010/usage/run.py \
  --arknet . --build /private/tmp/arknet-usage-check/build-standalone \
  --logs /private/tmp/arknet-usage-check/logs-standalone

cmake -S /private/tmp/arknet-usage-check/project \
  -B /private/tmp/arknet-usage-check/build-boost -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=/usr/bin/c++ \
  -DARKNET_ENABLE_SSL=ON -DARKNET_USE_BOOST_ASIO=ON \
  -DBoost_DIR=/opt/homebrew/lib/cmake/Boost-1.90.0 \
  -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 \
  > /private/tmp/arknet-usage-check/boost-configure.log 2>&1
cmake --build /private/tmp/arknet-usage-check/build-boost --parallel 3 \
  > /private/tmp/arknet-usage-check/boost-build.log 2>&1
python3 -B tests/results/macmini-m4pro-20261010/usage/run.py \
  --arknet . --build /private/tmp/arknet-usage-check/build-boost \
  --logs /private/tmp/arknet-usage-check/logs-boost
```
