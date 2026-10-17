# 历史性能报告

本目录保留 Markdown 历史报告。原始 JSON、命令、源码快照和日志可从[固定归档提交](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results)或现有 [v0.2.1 发布附件](https://github.com/OpenArkStudio/arknet/releases/tag/v0.2.1)获取。

- `arknet-0.2.1-benchmark-comparison.tar.gz` 内含 `source/arknet-measured-2aa5bc0.tar`。
- `arknet-0.2.1-validation-logs.tar.gz` 内含 `historical/arknet-0.2.1-validation-logs/source/arknet-validation-6c8418e.tar`。
- 两个嵌套源码归档均完整保留历史 `benchmarks/results/` 与 `tests/results/` 目录。使用前核对发布附件的 `.sha256` 文件及归档内的 `SHA256SUMS`。

归档提交只用于定位存储，不代表每份报告的测量源码。复现时保留数据集原有的源码版本、可执行文件校验和、命令、机器信息和测量时间。

本地已有该归档提交时，可仅将证据目录提取到源码仓库外：

```sh
history_dir=/path/to/outside-source/arknet-history
mkdir -p "$history_dir"
git archive 7685e241bebd086d50da403f2cf06a72bbef1085 \
  benchmarks/results tests/results | tar -xf - -C "$history_dir"
```

新的原始 JSON 和日志保存到源码仓库外，通过发布附件或 CI artifacts 共享。[公开性能报告](../../docs/performance/index_CN.md)、SVG 图表和精简数值汇总继续保存在 `docs/`，确定性输入保存在对应测试中。C++ 基准程序仍可独立编译运行；可选的 shell 和离线报告工具读取调用者指定的外部目录。发布归档后再设置 `--raw-base` 或 RUDP 报告工具的 `--raw-url`，未设置时只显示输入名称，不生成原始数据链接。
