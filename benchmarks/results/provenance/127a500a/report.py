#!/usr/bin/env python3
"""Generate bilingual, quantitative documentation from complete local runs."""

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import statistics

METRICS = ("roundtrips_per_second", "payload_mib_per_second", "rtt_p50_us", "rtt_p95_us", "rtt_p99_us", "peak_rss_bytes")
PROTOCOLS = ("tcp", "udp", "websocket", "tcps", "wss", "http", "https")
TOPOLOGIES = (("shared", 1), ("shared", 2), ("shared", 4), ("sharded", 2), ("sharded", 4))
MODEL_LABELS = ("1 context\n1 thread", "1 context\n2 threads", "1 context\n4 threads",
                "2 contexts\n2 threads", "4 contexts\n4 threads")
MODEL_COLORS = ("#68737d", "#458f82", "#206db0", "#b87727", "#aa4b57")


def load_reports(paths):
    reports = []
    groups = defaultdict(list)
    identities = set()
    for path in paths:
        report = json.loads(path.read_text(encoding="utf-8"))
        if report.get("complete") is not True or not report.get("source_sha256"):
            raise ValueError(f"{path}: complete run with source hash required")
        entries = report.get("results", [])
        settings = report["settings"]
        repetitions = settings["repetitions"]
        if not entries or repetitions < 2:
            raise ValueError(f"{path}: at least two repetitions required")
        seen = set()
        for entry in entries:
            metric = entry["metrics"]
            if entry.get("returncode") != 0 or entry.get("error") or metric.get("ok") is not True:
                raise ValueError(f"{path}: failed measurement")
            key = (metric["backend"], metric.get("implementation", "arknet-callback"),
                   metric["protocol"], metric["payload_bytes"], metric["clients"], metric["window"],
                   metric["io_model"], metric["io_threads"], metric["handler_work"])
            identity = (key, entry["repetition"])
            if identity in identities or not 1 <= entry["repetition"] <= repetitions:
                raise ValueError(f"{path}: duplicate or invalid repetition")
            seen.add(identity)
            identities.add(identity)
            for field in METRICS:
                value = metric.get(field)
                if not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
                    raise ValueError(f"{path}: invalid {field}")
            groups[key].append(metric)
        for key in {identity[0] for identity in seen}:
            if sum(identity[0] == key for identity in seen) != repetitions:
                raise ValueError(f"{path}: missing repetition")
        reports.append((path, report))
    if len({report["source_sha256"] for _, report in reports}) != 1:
        raise ValueError("source hashes differ; do not compare different implementations")
    if len({json.dumps(report["host"], sort_keys=True) for _, report in reports}) != 1:
        raise ValueError("hosts differ; generate separate reports")
    if len({(report["settings"]["seconds"], report["settings"]["warmup"], report["settings"]["repetitions"])
            for _, report in reports}) != 1:
        raise ValueError("measurement durations/repetitions differ")
    return reports, groups


def summary(groups):
    rows = []
    for key, values in sorted(groups.items()):
        row = dict(zip(("backend", "implementation", "protocol", "payload_bytes", "clients", "window", "io_model", "io_threads", "handler_work"), key))
        row["repetitions"] = len(values)
        for metric in METRICS:
            samples = [value[metric] for value in values]
            row[metric] = {"median": statistics.median(samples), "min": min(samples), "max": max(samples)}
        cpu = [value.get("cpu_seconds", value.get("total_cpu_seconds")) for value in values]
        row["cpu_seconds"] = statistics.median(cpu) if all(isinstance(value, (int, float)) and math.isfinite(value) for value in cpu) else None
        rows.append(row)
    return rows


def chart(rows, output, metric, title, work=None):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, MaxNLocator

    subset = [row for row in rows if row["clients"] == 16 and row["payload_bytes"] == 1024
              and (work is None or row["handler_work"] == work)]
    providers = sorted({row["backend"] for row in subset})
    if not providers:
        raise ValueError("chart requires the 1024 B / 16 clients profile")
    fig, axes = plt.subplots(len(providers), 1, figsize=(8, 4.5 * len(providers)),
                             layout="constrained", squeeze=False, sharey=True)
    for axis, backend in zip(axes.flat, providers):
        values, positions, errors, colors = [], [], [[], []], []
        for index, topology in enumerate(TOPOLOGIES):
            matching = [row for row in subset if row["backend"] == backend
                        and (row["io_model"], row["io_threads"]) == topology]
            if not matching:
                continue
            if len(matching) != 1:
                raise ValueError("ambiguous chart profile")
            stats = matching[0][metric]
            positions.append(index)
            colors.append(MODEL_COLORS[index])
            values.append(stats["median"])
            errors[0].append(stats["median"] - stats["min"])
            errors[1].append(stats["max"] - stats["median"])
        bars = axis.bar(positions, values, width=0.6, yerr=errors, capsize=3, color=colors)
        axis.bar_label(bars, labels=[f"{value:,.0f}" for value in values], padding=4, fontsize=12)
        axis.set_xticks(range(len(TOPOLOGIES)), MODEL_LABELS, fontsize=12)
        windows = sorted({row["window"] for row in subset if row["backend"] == backend})
        axis.set_title(f"{backend} | 1024 B | 16 clients | window={','.join(map(str, windows))}", fontsize=14)
        axis.tick_params(axis="y", labelsize=12)
        axis.grid(axis="y", alpha=0.2)
        axis.set_axisbelow(True)
        axis.margins(y=.25)
        axis.yaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{value:,.0f}"))
        axis.yaxis.set_major_locator(MaxNLocator(5))
        if metric == "rtt_p99_us":
            axis.set_ylabel("p99 RTT (microseconds; lower is better)", fontsize=12)
        else:
            axis.set_ylabel("Completed round trips / s (higher is better)", fontsize=12)
    fig.suptitle(title + "\nMedian; error bars = min/max of repeated runs", fontsize=14)
    fig.savefig(output, dpi=150)
    plt.close(fig)


def table(rows, chinese):
    labels = ("后端 / 模型", "往返 / s", "MiB/s", "p50 / us", "p95 / us", "p99 / us", "CPU / s", "RSS / MiB") if chinese else ("Backend / model", "Round trips/s", "MiB/s", "p50 / us", "p95 / us", "p99 / us", "CPU / s", "RSS / MiB")
    lines = ["| " + " | ".join(labels) + " |", "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in rows:
        throughput = row["roundtrips_per_second"]
        cells = [f"{row['backend']} / {row['io_model']}:{row['io_threads']}",
                 f"{throughput['median']:,.0f} [{throughput['min']:,.0f}, {throughput['max']:,.0f}]",
                 f"{row['payload_mib_per_second']['median']:,.2f}"]
        cells.extend(f"{row[field]['median']:,.2f}" for field in ("rtt_p50_us", "rtt_p95_us", "rtt_p99_us"))
        cells.append(f"{row['cpu_seconds']:.3f}" if row["cpu_seconds"] is not None else "N/A")
        cells.append(f"{row['peak_rss_bytes']['median'] / 1048576:,.2f}")
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines)


def comparison_table(rows, chinese):
    profiles = defaultdict(dict)
    for row in rows:
        if row["payload_bytes"] == 1024 and row["clients"] == 16:
            key = (row["implementation"], row["protocol"], row["backend"], row["handler_work"])
            profiles[key][(row["io_model"], row["io_threads"])] = row
    labels = (("IO 模型", "往返/s（倍率）", "p99/us（倍率）") if chinese else
              ("IO model", "Round trips/s (ratio)", "p99/us (ratio)"))
    lines = []
    for (implementation, protocol, backend, work), topologies in sorted(profiles.items()):
        models = (("shared", 1), ("shared", 4), ("sharded", 4))
        if not all(model in topologies for model in models):
            continue
        name = "coroutine TCP" if implementation == "asio-coroutine" else protocol.upper()
        lines += [f"### {name} / {backend} / work={work}", "",
                  "| " + " | ".join(labels) + " |", "| --- | ---: | ---: |"]
        for model in models:
            contexts = 1 if model[0] == "shared" else model[1]
            cells = [f"{contexts} context / {model[1]} thread" + ("s" if model[1] > 1 else "")]
            for metric, precision in (("roundtrips_per_second", 0), ("rtt_p99_us", 2)):
                baseline = topologies[models[0]][metric]["median"]
                value = topologies[model][metric]["median"]
                cell = f"{value:,.{precision}f}" + (f" ({value / baseline:.2f}×)" if baseline > 0 else " (N/A)")
                cells.append(cell)
            lines.append("| " + " | ".join(cells) + " |")
        lines.append("")
    return "\n".join(lines)


def overview_chart(rows, output, metric="roundtrips_per_second"):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, MaxNLocator

    subset = [row for row in rows if row["implementation"] == "arknet-callback" and
              row["payload_bytes"] == 1024 and row["clients"] == 16 and row["handler_work"] == 0]
    providers = sorted({row["backend"] for row in subset})
    protocols = [name for name in PROTOCOLS if any(row["protocol"] == name for row in subset)]
    if not providers or not protocols:
        return False
    models = (("shared", 1), ("shared", 4), ("sharded", 4))
    labels = ("1 context / 1 thread", "1 context / 4 threads", "4 contexts / 4 threads")
    colors = (MODEL_COLORS[0], MODEL_COLORS[2], MODEL_COLORS[4])
    fig, axes = plt.subplots(len(providers), 1, figsize=(8, max(3.5, len(protocols) * 1.1) * len(providers)),
                             layout="constrained", squeeze=False, sharex=True)
    for axis, backend in zip(axes.flat, providers):
        for offset, (model, label, color) in enumerate(zip(models, labels, colors)):
            values, positions, errors = [], [], [[], []]
            for index, protocol in enumerate(protocols):
                matching = [row for row in subset if row["backend"] == backend and row["protocol"] == protocol
                            and (row["io_model"], row["io_threads"]) == model]
                if not matching:
                    continue
                if len(matching) != 1:
                    raise ValueError("ambiguous overview profile")
                stats = matching[0][metric]
                values.append(stats["median"])
                positions.append(index + (offset - 1) * .24)
                errors[0].append(stats["median"] - stats["min"])
                errors[1].append(stats["max"] - stats["median"])
            bars = axis.barh(positions, values, height=.22, xerr=errors, capsize=2, color=color, label=label)
            axis.bar_label(bars, labels=[f"{value:,.0f}" for value in values], padding=4, fontsize=12)
        axis.set_yticks(range(len(protocols)), [name.upper() for name in protocols], fontsize=12)
        axis.invert_yaxis()
        axis.set_title(backend, fontsize=14)
        axis.tick_params(axis="x", labelbottom=True, labelsize=12)
        axis.grid(axis="x", alpha=.2)
        axis.set_axisbelow(True)
        axis.margins(x=.3)
        axis.xaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{value:,.0f}"))
        axis.xaxis.set_major_locator(MaxNLocator(4))
        axis.set_xlabel("Completed round trips / s (higher is better)" if metric == "roundtrips_per_second"
                        else "p99 RTT (microseconds; lower is better)", fontsize=12)
    handles, labels = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, labels, loc="outside upper center", ncol=1, fontsize=12, title_fontsize=13,
               title="Local loopback | 1024 B | 16 clients\nUDP window=1; others window=16 | median; error bars=min/max")
    fig.savefig(output, dpi=150)
    plt.close(fig)
    return True


def protocol_page(name, rows, chinese):
    suffix = "_CN" if chinese else ""
    alternate = name + ("" if chinese else "_CN") + ".md"
    lines = [f"# {name.upper()} " + ("本地性能" if chinese else "Local Performance"), "",
             f"[{'English' if chinese else '中文'}]({alternate}) | [{'报告概览' if chinese else 'Report overview'}](overview{suffix}.md)", ""]
    if name == "coroutine":
        lines += ["原生 Asio TCP 协程夹具，分别运行轻处理（work=0）和 CPU 处理（work=10000）。" if chinese else "Native Asio TCP awaitable fixture with light handlers (work=0) and CPU work (work=10000).", "",
                  "window>1 的 RTT 是整批完成时间，与回调夹具的逐消息 RTT 不同。仅在相同夹具和负载内比较 IO 模型。" if chinese else "RTT with window>1 measures batch completion, unlike message RTT in the callback fixture. Compare IO models within the same fixture and load.", ""]
    else:
        lines += ["数据来自本机 Release 构建的独立回环进程，不来自 CI。" if chinese else "Measurements come from independent local Release loopback processes, not CI.", ""]
    lines += ["图表固定使用 1024 字节消息、16 个连接，横轴直接标明 context 和线程数。吞吐量越高越好，p99 延迟越低越好。全部消息大小与连接数的结果在图下表格中。" if chinese else "Charts hold payload at 1024 bytes and connections at 16; the x-axis names context and thread counts. Higher throughput and lower p99 are better. Tables below retain every payload and connection profile.", ""]
    works = sorted({row["handler_work"] for row in rows})
    for work in works:
        label = f"{name}-work{work}"
        lines += [f"## work={work}", "", f"![Throughput](assets/{label}-throughput.png)", "", f"![p99 RTT](assets/{label}-latency.png)", ""]
        for clients in (1, 16):
            for payload in (64, 1024, 16384):
                subset = [row for row in rows if row["handler_work"] == work and row["clients"] == clients and row["payload_bytes"] == payload]
                if subset:
                    lines += [f"### {payload} B / {clients} " + ("客户端" if chinese else "clients") + f" / window={subset[0]['window']}", "", table(subset, chinese), ""]
    lines += ["所有表格使用重复测量的中位数；吞吐量方括号为最小值与最大值。分位数是各次运行采样分位数的中位数，不是合并样本后的分位数。" if chinese else "Tables show medians across repeated runs; throughput brackets show min/max. Latency values are medians of per-run sampled percentiles, not percentiles of pooled samples.", ""]
    lines += ["CPU / s 是进程 CPU 时间总和，可大于墙钟时间。回调夹具统计测量与流量排空阶段；协程夹具统计预热与测量阶段，在关闭连接前结束，不能横跨夹具比较。" if chinese else "CPU / s sums process CPU time and can exceed wall time. The callback fixture includes measurement and traffic drain; the coroutine fixture includes warmup and measurement, ending before connection cleanup. Do not compare CPU across these fixtures.", ""]
    return "\n".join(lines)


def host_table(host, chinese):
    unknown = "未采集" if chinese else "Not recorded"
    memory = host.get("memory_bytes")
    system = "macOS" if host.get("system") == "Darwin" else host.get("system")
    system = " ".join(str(value) for value in (system, host.get("os_version") or host.get("release")) if value)
    if host.get("os_build"):
        system += f" ({host['os_build']})"
    fields = (
        ("机器型号" if chinese else "Machine model", host.get("model")),
        ("处理器" if chinese else "Processor", host.get("cpu") or host.get("processor")),
        ("架构" if chinese else "Architecture", host.get("machine")),
        ("物理 / 逻辑核心" if chinese else "Physical / logical CPUs",
         f"{host.get('physical_cpus', unknown)} / {host.get('logical_cpus', unknown)}"),
        ("内存" if chinese else "Memory", f"{memory / 1073741824:g} GiB" if isinstance(memory, int) else None),
        ("操作系统" if chinese else "Operating system", system or host.get("release")),
        ("内核版本" if chinese else "Kernel release", host.get("release")),
    )
    lines = ["| " + ("配置 | 实测机器 |" if chinese else "Configuration | Test machine |"), "| --- | --- |"]
    lines.extend(f"| {label} | {value or unknown} |" for label, value in fields)
    return "\n".join(lines)


def key_findings(rows, chinese):
    profiles = defaultdict(list)
    providers = {row["backend"] for row in rows}
    backend = "standalone" if "standalone" in providers else next(iter(sorted(providers)), None)
    for row in rows:
        if row["backend"] == backend and row["payload_bytes"] == 1024 and row["clients"] == 16:
            profiles[(row["implementation"], row["protocol"], row["handler_work"], row["window"])].append(row)
    lines = []
    for (implementation, protocol, work, window), values in sorted(profiles.items()):
        if protocol not in ("tcp", "udp"):
            continue
        baselines = [row for row in values if (row["io_model"], row["io_threads"]) == ("shared", 1)]
        if len(baselines) != 1 or len(values) < 2:
            continue
        baseline = baselines[0]
        best = max(values, key=lambda row: row["roundtrips_per_second"]["median"])
        rate = best["roundtrips_per_second"]["median"]
        base_rate = baseline["roundtrips_per_second"]["median"]
        ratio = f"{rate / base_rate:.2f}" if base_rate else "N/A"
        contexts = 1 if best["io_model"] == "shared" else best["io_threads"]
        name = ("协程 TCP" if chinese else "Coroutine TCP") if implementation == "asio-coroutine" else protocol.upper()
        latency = best["rtt_p99_us"]["median"]
        base_latency = baseline["rtt_p99_us"]["median"]
        if chinese:
            lines.append(f"- **{name} / {backend} / work={work} / window={window}**：本组最高吞吐量中位数为 {rate:,.0f} 往返/s（{contexts} 个 context、{best['io_threads']} 个线程），单线程为 {base_rate:,.0f}，倍率 {ratio}×。对应 p99 为 {latency:,.2f} us，单线程为 {base_latency:,.2f} us。")
        else:
            lines.append(f"- **{name} / {backend} / work={work} / window={window}**: the highest median throughput in this profile is {rate:,.0f} round trips/s ({contexts} contexts, {best['io_threads']} threads), versus {base_rate:,.0f} with one thread ({ratio}×). Its p99 is {latency:,.2f} us versus {base_latency:,.2f} us with one thread.")
    return "\n".join(lines)


def overview(reports, rows, chinese, raw_base, host_description):
    suffix = "_CN" if chinese else ""
    first = reports[0][1]
    settings = first["settings"]
    lines = ["# " + ("本地性能报告" if chinese else "Local Performance Report"), "",
             f"[{'English' if chinese else '中文'}](overview{'' if chinese else '_CN'}.md) | [{'测量方法' if chinese else 'Methodology'}](../testing{suffix}.md)", "",
             "本报告比较 arknet 在同一台机器上的吞吐量、尾延迟和 IO 调度模型，数据来自本地测试。" if chinese else "This report measures local arknet throughput, tail latency and IO scheduler scaling on one machine.", "",
             "## " + ("测试条件" if chinese else "Test Conditions"), "",
             host_table(first["host"], chinese), "",
             (f"Release 构建；测量 {settings['seconds']} s，预热 {settings['warmup']} s，重复 {settings['repetitions']} 次。" if chinese else f"Release build; measurement={settings['seconds']} s; warmup={settings['warmup']} s; repetitions={settings['repetitions']}."), "",
             "主图固定为 **1024 字节消息、16 个连接**。一次请求与回显算一次往返；TCP、WebSocket 和 HTTP 每连接最多 16 条在途消息，UDP 为 1 条。客户端与服务端运行在同一进程、同一套 IO 线程上，使用本机回环网络。" if chinese else "Main charts use **1024-byte messages and 16 connections**. One request plus echo is one round trip. TCP, WebSocket and HTTP allow 16 messages in flight per connection; UDP allows one. Both peers share the process and IO workers on the local loopback network.", "",
             "## " + ("核心结果" if chinese else "Key Results"), "",
             "work=0 表示没有额外业务计算；work=10000 表示每次服务端回显增加 10000 次确定性 CPU 计算。协程结果使用原生 Asio TCP 夹具，公开协程端点 API 仍在计划中。" if chinese else "work=0 adds no business computation; work=10000 adds 10000 deterministic CPU iterations per server echo. Coroutine results use a native Asio TCP fixture; public coroutine endpoint APIs remain planned.", "",
             key_findings(rows, chinese), "",
             "以上列出默认 standalone 后端的部分负载；若报告仅有 Boost，则使用 Boost。最高中位数只适用于对应负载，选型还需结合 p99 与其他消息大小。" if chinese else "These examples use the default standalone backend, or Boost when it is the only backend. The highest median applies to its specific load; also check p99 and other payloads before choosing a model.", ""]
    if any(row["implementation"] == "arknet-callback" for row in rows):
        lines += ["## " + ("吞吐量：每秒完成多少次请求与回显" if chinese else "Throughput: Completed Requests and Echoes per Second"), "",
                  "柱子越长，吞吐量越高。图例直接标明 context 和线程数，柱末标注实际数值。每种协议内比较不同 IO 模型；UDP 与其他协议的在途窗口不同。" if chinese else "Longer bars mean higher throughput. The legend names context and thread counts; labels show actual values. Compare IO models within each protocol; UDP has a different inflight window.", "",
                  "![Local throughput overview](assets/overview-throughput.png)", "",
                  "## " + ("p99 延迟：99% 的采样往返在多久内完成" if chinese else "p99 Latency: Time Within Which 99% of Samples Finish"), "",
                  "p99 表示约 99% 的采样往返在该时间内完成，单位为微秒（1000 us = 1 ms）。柱子越短，尾延迟越低。吞吐量提高可能伴随尾延迟上升，两张图需要一起阅读。" if chinese else "About 99% of sampled round trips finish within the p99 time. Units are microseconds (1000 us = 1 ms); shorter bars mean lower tail latency. Higher throughput can accompany worse tail latency, so read both charts.", "",
                  "![Local p99 latency overview](assets/overview-latency.png)", ""]
    lines += ["## " + ("如何选择 IO 模型" if chinese else "Choosing an IO Model"), "",
              ("| 模型 | 对应业务 | 需要注意 |\n| --- | --- | --- |\n| 1 context / 1 thread | 低并发、控制服务、较轻的状态处理 | 耗时回调会影响所有连接 |\n| 1 context / 多线程 | 连接活跃程度不均衡、希望共享空闲线程 | 使用每连接 strand；共享调度有开销 |\n| 多 context / 各 1 thread | 分区会话、租户或负载分配稳定的服务 | 繁忙分片无法借用其他分片线程 |" if chinese else
               "| Model | Workload | Tradeoff |\n| --- | --- | --- |\n| 1 context / 1 thread | Low concurrency, control services, light state handling | Long handlers delay every connection |\n| 1 context / several threads | Uneven connection activity, shared available workers | Use per-connection strands; shared scheduling has overhead |\n| Several contexts / 1 thread each | Partitioned sessions, tenants or stable assignment | A busy shard cannot borrow other shards' workers |"), "",
              f"[{'协程的 IO 与 CPU 负载比较' if chinese else 'Coroutine IO and CPU workload comparisons'}](../threading{suffix}.md).", "",
              "## " + ("各协议的详细结果" if chinese else "Detailed Results by Protocol"), ""]
    names = [name for name in (*PROTOCOLS, "coroutine") if any((row["implementation"] == "asio-coroutine" if name == "coroutine" else row["implementation"] == "arknet-callback" and row["protocol"] == name) for row in rows)]
    for name in names:
        lines.append(f"- [{name.upper()}](./{name}{suffix}.md)")
    lines += ["", "每个详细页记录全部消息大小（64、1024、16384 字节）、单连接与 16 连接的测试，含吞吐量、p50/p95/p99、CPU 和内存数据。图表使用 1024 B / 16 连接负载，中位数和误差线分别表示三次运行的中位数与最小／最大值，误差线不是置信区间。" if chinese else "Detailed pages contain all payloads (64, 1024 and 16384 bytes), one-connection and 16-connection tests, with throughput, p50/p95/p99, CPU and memory metrics. Charts use 1024 B / 16 connections. Bars show repeated-run medians; error bars show min/max, not confidence intervals.", "",
              "## " + ("数值与提升倍率" if chinese else "Values and Scaling Ratios"), "",
              "倍率以相同夹具、后端和 work 的单线程模型为 1.00×。吞吐量倍率越大越好，p99 延迟倍率越小越好。shared:1 是单 context 单线程，shared:4 是单 context 四线程，sharded:4 是四个 context 各一个线程。" if chinese else "Ratios use the one-thread model as 1.00× within the same fixture, backend and work. Higher throughput ratios and lower p99 ratios are better. shared:1 is one context/one thread; shared:4 is one context/four threads; sharded:4 is four contexts/one thread each.", "",
              comparison_table(rows, chinese), "",
              "协程 window=16 的 RTT 是整批完成时间，回调夹具是逐消息 RTT。不能据此判断协程相对回调的加速比。" if chinese else "Coroutine RTT with window=16 measures batch completion; callback RTT measures each message. These fixtures do not establish coroutine-versus-callback speedups.", "",
              "## " + ("工具链与构建" if chinese else "Toolchain and Build"), "",
              host_description, "",
              "两种后端使用的 Beast 版本不同，后端之间的性能差异同时包含依赖版本的影响，不能全部归因于 Asio。" if chinese else "The backends use different Beast versions, so differences between backends also include dependency-version effects and cannot be attributed solely to Asio.", "",
              "## " + ("指标定义" if chinese else "Metric Definitions"), "",
              "MiB/s 统计双向应用数据（2 × payload × 完成往返数），不含协议头部和加密开销。RTT 包含排队、拷贝、业务处理与回显；RSS 是进程生命周期峰值。延迟表格是各次运行采样分位数的中位数，不是合并样本的分位数。" if chinese else "MiB/s counts bidirectional application payload (2 × payload × completed round trips), excluding framing/encryption overhead. RTT includes queueing, copies, handler work and echo; RSS is the process lifetime peak. Latency tables use medians of per-run sampled percentiles, not percentiles of pooled samples.", "",
              "## " + ("原始数据与复现" if chinese else "Raw Data and Reproduction"), "",
              f"- Source SHA-256: `{first['source_sha256']}`",
              f"- Completed measurements: {sum(len(report['results']) for _, report in reports)}",
              f"- [{'复现命令与检查范围' if chinese else 'Commands and validation scope'}](../testing{suffix}.md)", ""]
    for path, report in reports:
        lines.append(f"- [{path.name}]({raw_base.rstrip('/')}/{path.name}): {len(report['results'])} measurements; {report['recorded_at_utc']}; executable SHA-256 `{report['executable_sha256']}`")
    lines += ["", "## " + ("适用范围" if chinese else "Scope"), "",
              "这是本机回环基线，不是生产容量承诺。短时间测量、调度和温度会带来波动；按消息大小、并发和实际业务处理选择模型，再在目标机器复测。协程夹具与回调夹具不同，不据此宣称协程更快。" if chinese else "This is a local loopback baseline, not a production capacity promise. Short runs, scheduling and thermals cause variance. Choose by payload, concurrency and handler work, then repeat on deployment hardware. The coroutine and callback fixtures differ; these results do not establish coroutine speedups.", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", nargs="+", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--raw-base", default="https://github.com/OpenArkStudio/arknet/blob/main/benchmarks/results")
    parser.add_argument("--host-description", required=True, help="hardware, compiler, dependencies and build flags")
    args = parser.parse_args()
    reports, groups = load_reports(args.reports)
    rows = summary(groups)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    assets = args.output_dir / "assets"
    assets.mkdir(exist_ok=True)
    (args.output_dir / "summary.json").write_text(json.dumps(rows, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    overview_chart(rows, assets / "overview-throughput.png")
    overview_chart(rows, assets / "overview-latency.png", "rtt_p99_us")
    for name in (*PROTOCOLS, "coroutine"):
        subset = [row for row in rows if (row["implementation"] == "asio-coroutine" if name == "coroutine" else row["implementation"] == "arknet-callback" and row["protocol"] == name)]
        if not subset:
            continue
        for work in sorted({row["handler_work"] for row in subset}):
            label = f"{name}-work{work}"
            chart(subset, assets / f"{label}-throughput.png", "roundtrips_per_second", f"{name.upper()} | work={work}", work)
            chart(subset, assets / f"{label}-latency.png", "rtt_p99_us", f"{name.upper()} | work={work}", work)
        for chinese in (False, True):
            suffix = "_CN" if chinese else ""
            (args.output_dir / f"{name}{suffix}.md").write_text(protocol_page(name, subset, chinese), encoding="utf-8")
    for chinese in (False, True):
        suffix = "_CN" if chinese else ""
        (args.output_dir / f"overview{suffix}.md").write_text(overview(reports, rows, chinese, args.raw_base, args.host_description), encoding="utf-8")
    print(f"Generated {len(rows)} comparison rows in {args.output_dir}")


if __name__ == "__main__":
    main()
