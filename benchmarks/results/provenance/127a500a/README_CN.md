# 测量源码记录

与相邻 `fa69d6ce` 记录完全一致的归档输入可由该目录复用；原始文件哈希不变。

[English](README.md)

`macmini-m4pro-20261010-*.json` 基线包含 1,620 次测量，记录的源码
SHA-256 为 `127a500ad7e2fbe4c831cc3ebacc63acac96013e2cb8bf3893d7997ca6ea7f56`。

`manifest.json` 列出每个测量输入及其 SHA-256。本目录保留原始运行脚本、
报告脚本和协程程序；后续修改用于 SVG 报告和扩展负载测试。原始测量数据和
元数据保持不变。扩展矩阵独立保存在
`benchmarks/results/macmini-m4pro-20261010-dimensions/`。

在仓库根目录验证测量源码：

```sh
python3 benchmarks/results/provenance/127a500a/verify.py
```

验证按清单读取原始文件列表，用归档版本替代已修改的输入；新增基准工具不会
影响重建的基线哈希。未修改的网络头文件和构建输入从当前仓库读取。
历史诊断数据保存在 `benchmarks/results/diagnostics/`。
