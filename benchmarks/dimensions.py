#!/usr/bin/env python3
"""Validate the expanded load matrix and generate bilingual SVG reports."""

import argparse
from collections import defaultdict
from itertools import product
import json
import math
from pathlib import Path
from urllib.parse import quote

import report

PAYLOADS = (64, 1024, 16384)
WINDOWS = (1, 4, 16, 64)
BACKENDS = ("standalone", "boost")
TOPOLOGIES = (("shared", 1), ("shared", 4), ("sharded", 4))
REPETITIONS = 3
FIELDS = ("backend", "implementation", "protocol", "payload_bytes", "clients",
          "window", "io_model", "io_threads", "handler_work")
COUNTERS = ("client_send_rejections", "client_send_completion_errors", "server_send_rejections",
            "server_send_completion_errors", "content_errors", "content_or_io_errors", "order_errors", "unexpected_messages",
            "disconnect_errors", "udp_lost_messages", "unfinished_messages", "callback_exceptions",
            "drain_timeouts", "duplicate_completions", "pending_completions", "warmup_errors")
COUNTER_LABELS = {
    "client_send_rejections": ("Client sends rejected", "客户端发送被拒绝"),
    "client_send_completion_errors": ("Client send completion errors", "客户端发送完成错误"),
    "server_send_rejections": ("Server sends rejected", "服务端发送被拒绝"),
    "server_send_completion_errors": ("Server send completion errors", "服务端发送完成错误"),
    "content_errors": ("Content errors", "内容错误"),
    "content_or_io_errors": ("Content or IO errors", "内容或 IO 错误"),
    "order_errors": ("Message order errors", "消息顺序错误"),
    "unexpected_messages": ("Unexpected messages", "意外消息"),
    "disconnect_errors": ("Disconnect errors", "断开连接错误"),
    "udp_lost_messages": ("UDP lost messages", "UDP 丢失报文"),
    "unfinished_messages": ("Unfinished messages", "未完成消息"),
    "callback_exceptions": ("Callback exceptions", "回调异常"),
    "drain_timeouts": ("Drain timeouts", "等待在途消息完成超时"),
    "duplicate_completions": ("Duplicate completions", "重复完成通知"),
    "pending_completions": ("Pending completions", "未完成通知"),
    "warmup_errors": ("Warmup errors", "预热错误"),
}
COLORS = ("#68737d", "#206db0", "#aa4b57")
METRIC_FILES = (("roundtrips_per_second", "throughput"),
                ("payload_mib_per_second", "payload"), ("rtt_p99_us", "latency"))
NAMES = (*report.PROTOCOLS, "native-tcp")


def finite(value, positive=False):
    return (type(value) in (int, float) and math.isfinite(value)
            and (value > 0 if positive else value >= 0))


def reject_constant(value):
    raise ValueError(f"non-finite JSON constant: {value}")


def model_label(model, threads, chinese=False):
    if model == "sharded":
        return (f"{threads} 个 io_context 各 1 个线程" if chinese
                else f"{threads} contexts / one thread each")
    return report.model_name(model, threads, chinese)


def work_label(work, chinese=False):
    if work == 0:
        return "无额外业务计算" if chinese else "No extra application computation"
    return (f"每次回显增加 {work:,} 次确定性运算" if chinese
            else f"{work:,} deterministic iterations per echo")


def configuration(entry):
    for field in ("payload_bytes", "clients", "window", "io_threads", "handler_work"):
        if type(entry.get(field)) is not int:
            raise ValueError(f"invalid configuration field: {field}")
    if entry.get("protocol") not in report.PROTOCOLS or entry.get("io_model") not in ("shared", "sharded"):
        raise ValueError("invalid protocol or IO model")
    execution = entry.get("execution")
    if execution not in (None, "callback", "coroutine"):
        raise ValueError("invalid native execution mode")
    implementation = "arknet-callback" if execution is None else "asio-" + execution
    return (implementation, entry["protocol"], entry["payload_bytes"], entry["clients"],
            entry["window"], entry["io_model"], entry["io_threads"], entry["handler_work"])


def expected_configurations(settings):
    if settings.get("repetitions") != REPETITIONS or type(settings.get("repetitions")) is not int:
        raise ValueError("the dimension matrix requires three repetitions")
    if not finite(settings.get("seconds"), positive=True) or not finite(settings.get("warmup")):
        raise ValueError("invalid measurement or warmup duration")
    payloads = settings.get("payloads", [])
    if sorted(payloads) != list(PAYLOADS) or any(type(value) is not int for value in payloads):
        raise ValueError("requested payload matrix must be 64, 1024 and 16384 bytes")
    profiles = settings.get("profiles", [])
    if (len(profiles) != len(WINDOWS) or
            sorted((profile.get("clients"), profile.get("window")) for profile in profiles)
            != [(16, window) for window in WINDOWS] or
            any(type(profile.get(field)) is not int for profile in profiles for field in ("clients", "window"))):
        raise ValueError("requested client/window matrix must be 16 clients with windows 1, 4, 16, 64")
    if settings.get("udp_throughput_window") is not None:
        raise ValueError("explicit windows are required for UDP too")
    protocols = settings.get("protocols", [])
    if not protocols or any(value not in report.PROTOCOLS for value in protocols) or len(set(protocols)) != len(protocols):
        raise ValueError("invalid requested protocols")
    executions = settings.get("execution_modes")
    if executions is None:
        implementations, works = ("arknet-callback",), (0,)
    elif sorted(executions) == ["callback", "coroutine"] and protocols == ["tcp"]:
        implementations, works = ("asio-callback", "asio-coroutine"), (0, 10000)
    else:
        raise ValueError("native TCP requires both callback and coroutine executions")
    requested_io = settings.get("io_profiles", [])
    expected_io = {(model, threads, work) for (model, threads), work in product(TOPOLOGIES, works)}
    if (len(requested_io) != len(expected_io) or
            {tuple(profile.get(field) for field in ("io_model", "io_threads", "handler_work"))
             for profile in requested_io} != expected_io or
            any(type(profile.get(field)) is not int for profile in requested_io for field in ("io_threads", "handler_work"))):
        raise ValueError("requested IO models or computation amounts differ from the dimension matrix")
    return {(implementation, protocol, payload, 16, window, model, threads, work)
            for implementation, protocol, payload, window, (model, threads), work in
            product(implementations, protocols, PAYLOADS, WINDOWS, TOPOLOGIES, works)}


def failed_measurement(entry):
    metric = entry.get("metrics") or {}
    for field in COUNTERS:
        if field in metric and (type(metric[field]) is not int or metric[field] < 0):
            raise ValueError(f"invalid error counter: {field}")
    counters = {field: metric[field] for field in COUNTERS if metric.get(field, 0) != 0}
    error = entry.get("error") or metric.get("error")
    if (entry.get("returncode") == 0 and metric.get("ok") is True and not error
            and not counters and metric.get("warmup_ok", True) is True):
        return None
    return {"repetition": entry["repetition"], "returncode": entry.get("returncode"),
            "error": str(error or "measurement failed validation"), "counters": counters}


def load_reports(paths):
    reports, samples, failures = [], defaultdict(dict), defaultdict(list)
    identities = set()
    for path in paths:
        value = json.loads(path.read_text(encoding="utf-8"), parse_constant=reject_constant)
        if value.get("schema_version") != 2 or value.get("complete") is not True or not value.get("source_sha256"):
            raise ValueError(f"{path}: complete schema 2 report with source hash required")
        expected = expected_configurations(value["settings"])
        entries = value.get("results", [])
        metric_backends = {entry.get("metrics", {}).get("backend") for entry in entries
                           if isinstance(entry.get("metrics"), dict) and entry["metrics"].get("backend")}
        if value.get("backend"):
            metric_backends.add(value["backend"])
        if len(metric_backends) != 1 or not metric_backends.issubset(BACKENDS):
            raise ValueError(f"{path}: one explicit, consistent backend required")
        backend = next(iter(metric_backends))
        seen = set()
        for entry in entries:
            config = configuration(entry)
            repetition = entry.get("repetition")
            if config not in expected or type(repetition) is not int or not 1 <= repetition <= REPETITIONS:
                raise ValueError(f"{path}: unexpected configuration or repetition")
            identity = (config, repetition)
            global_identity = (backend, config, repetition)
            if global_identity in identities:
                raise ValueError(f"{path}: duplicate measurement")
            seen.add(identity)
            identities.add(global_identity)
            key = (backend, *config)
            metric = entry.get("metrics")
            if metric is not None:
                if not isinstance(metric, dict):
                    raise ValueError(f"{path}: metrics must be an object")
                for field, requested in zip(FIELDS, key):
                    if field in metric and metric[field] != requested:
                        raise ValueError(f"{path}: metrics disagree with requested {field}")
            failure = failed_measurement(entry)
            if failure is not None:
                failures[key].append(failure)
                continue
            for field, requested in zip(FIELDS, key):
                if metric.get(field, "arknet-callback" if field == "implementation" else None) != requested:
                    raise ValueError(f"{path}: successful metrics missing requested {field}")
            for field in report.METRICS:
                if not finite(metric.get(field), positive=field in ("roundtrips_per_second", "payload_mib_per_second")):
                    raise ValueError(f"{path}: invalid successful {field}")
            if not finite(metric.get("elapsed_seconds"), positive=True) or not finite(metric.get("messages"), positive=True):
                raise ValueError(f"{path}: successful measurement contains no completed traffic")
            if key[1].startswith("asio-") and metric.get("latency_mode") != ("message" if entry["window"] == 1 else "batch"):
                raise ValueError(f"{path}: native latency mode disagrees with the requested window")
            samples[key][repetition] = dict(metric, _repetition=repetition)
        if seen != {(config, repetition) for config in expected for repetition in range(1, REPETITIONS + 1)}:
            raise ValueError(f"{path}: missing requested configuration or repetition")
        reports.append((path, value))
    all_expected = set()
    for backend, protocol, payload, window, (model, threads) in product(BACKENDS, report.PROTOCOLS, PAYLOADS, WINDOWS, TOPOLOGIES):
        all_expected.add((backend, "arknet-callback", protocol, payload, 16, window, model, threads, 0))
    for backend, execution, payload, window, (model, threads), work in product(BACKENDS, ("callback", "coroutine"), PAYLOADS, WINDOWS, TOPOLOGIES, (0, 10000)):
        all_expected.add((backend, "asio-" + execution, "tcp", payload, 16, window, model, threads, work))
    if set(samples) | set(failures) != all_expected:
        raise ValueError("missing requested protocol, backend or native execution configuration")
    report.validate_address_families(reports)
    if len({value["source_sha256"] for _, value in reports}) != 1:
        raise ValueError("source hashes differ")
    if len({json.dumps(value.get("host"), sort_keys=True) for _, value in reports}) != 1:
        raise ValueError("hosts differ")
    if not reports or not isinstance(reports[0][1].get("host"), dict):
        raise ValueError("recorded host metadata required")
    if len({(value["settings"]["seconds"], value["settings"]["warmup"], value["settings"]["repetitions"])
            for _, value in reports}) != 1:
        raise ValueError("measurement durations or repetitions differ")
    valid = {key: list(values.values()) for key, values in samples.items() if key not in failures}
    failed = [dict(zip(FIELDS, key), repetitions=REPETITIONS,
                   successful_repetitions=len(samples.get(key, {})), failures=values)
              for key, values in sorted(failures.items())]
    return reports, valid, failed


def paired_ratios(groups):
    rows = []
    for key, values in sorted(groups.items()):
        if key[1] != "asio-coroutine":
            continue
        callback_key = (key[0], "asio-callback", *key[2:])
        if callback_key not in groups:
            continue
        callback = {value["_repetition"]: value for value in groups[callback_key]}
        coroutine = {value["_repetition"]: value for value in values}
        if set(callback) != set(coroutine) or len(callback) != REPETITIONS:
            raise ValueError("matched ratios require all paired repetitions")
        if any(not finite(value["roundtrips_per_second"], positive=True)
               for value in (*callback.values(), *coroutine.values())):
            raise ValueError("matched ratios require positive finite throughput")
        ratios = [coroutine[index]["roundtrips_per_second"] / callback[index]["roundtrips_per_second"]
                  for index in sorted(callback)]
        cpu_pairs = [(report.cpu_usage(callback[index]), report.cpu_usage(coroutine[index]))
                     for index in sorted(callback)]
        complete_cpu = all(left is not None and right is not None and left[0] > 0
                           and finite(right[0] / left[0]) for left, right in cpu_pairs)
        rows.append(dict(zip(FIELDS, key), implementation="paired-native", repetitions=REPETITIONS,
                         coroutine_callback_ratio=report.run_statistics(ratios),
                         coroutine_callback_cpu_ratio=(report.run_statistics([right[0] / left[0] for left, right in cpu_pairs])
                                                       if complete_cpu else None),
                         cpu_percent_estimated=(any(left[1] or right[1] for left, right in cpu_pairs)
                                                if complete_cpu else None)))
    return rows


def belongs(row, name):
    return (row["implementation"] != "arknet-callback" and row["protocol"] == "tcp"
            if name == "native-tcp" else row["implementation"] == "arknet-callback" and row["protocol"] == name)


def chart(rows, failed, output, name, metric, work=0):
    import matplotlib
    matplotlib.use("Agg")
    matplotlib.rcParams["svg.fonttype"] = "none"
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    from matplotlib.ticker import FuncFormatter, MaxNLocator

    native = name == "native-tcp"
    ratio = metric in ("coroutine_callback_ratio", "coroutine_callback_cpu_ratio")
    cpu = metric in ("cpu_percent", "coroutine_callback_cpu_ratio")
    executions = ("paired-native",) if ratio else (("asio-callback", "asio-coroutine") if native else ("arknet-callback",))
    styles = {"arknet-callback": "-", "asio-callback": "-", "asio-coroutine": "--", "paired-native": "-"}
    fig, axes = plt.subplots(3, 2, figsize=(12, 12), sharex=True, sharey="row")
    for payload_index, row_axes in enumerate(axes):
        for backend_index, axis in enumerate(row_axes):
            payload, backend = PAYLOADS[payload_index], BACKENDS[backend_index]
            facet = [row for row in rows if belongs(row, name) and row["backend"] == backend
                     and row["payload_bytes"] == payload and row["handler_work"] == work
                     and row.get(metric) is not None]
            for (model, threads), color in zip(TOPOLOGIES, COLORS):
                for execution in executions:
                    matching = {row["window"]: row[metric] for row in facet
                                if row["implementation"] == execution and row["io_model"] == model
                                and row["io_threads"] == threads}
                    if not matching:
                        continue
                    values = [matching.get(window) for window in WINDOWS]
                    medians = [value["median"] if value else math.nan for value in values]
                    errors = [[value["median"] - value["min"] if value else math.nan for value in values],
                              [value["max"] - value["median"] if value else math.nan for value in values]]
                    axis.errorbar(WINDOWS, medians, yerr=errors, color=color,
                                  linestyle=styles[execution], marker="o", markersize=4, capsize=3)
            invalid_windows = sorted({row["window"] for row in failed if belongs(row, name)
                                      and row["backend"] == backend and row["payload_bytes"] == payload
                                      and row["handler_work"] == work})
            if invalid_windows and not ratio:
                axis.plot(invalid_windows, [.035] * len(invalid_windows), "x", color="#b33e4a",
                          transform=axis.get_xaxis_transform(), clip_on=False)
            if not facet:
                axis.text(.5, .5, "CPU N/A" if cpu else "No valid measurements", transform=axis.transAxes, ha="center")
            axis.set_title(f"{payload:,} B | {report.backend_name(backend)}", fontsize=12)
            axis.set_xscale("log", base=4)
            axis.set_xticks(WINDOWS, [str(window) for window in WINDOWS])
            axis.tick_params(axis="x", labelbottom=True)
            axis.grid(alpha=.2)
            axis.set_axisbelow(True)
            axis.set_ylim(bottom=0)
            axis.yaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{value:,.2f}" if ratio else f"{value:,.1f}" if cpu else f"{value:,.0f}"))
            axis.yaxis.set_major_locator(MaxNLocator(5))
            axis.set_xlabel("Requests per batch" if native else "Outstanding requests per connection")
            labels = {"roundtrips_per_second": "Completed round trips / s",
                      "payload_mib_per_second": "Application payload MiB/s (both directions)",
                      "rtt_p99_us": f"p99 {'batch' if native else 'message'} RTT (microseconds)",
                      "coroutine_callback_ratio": "Coroutine / callback throughput",
                      "cpu_percent": "Average process CPU (%)",
                      "coroutine_callback_cpu_ratio": "Coroutine / callback process CPU"}
            axis.set_ylabel(labels[metric], fontsize=10)
            if ratio:
                axis.axhline(1, color="#68737d", linestyle=":", linewidth=1)
    legend = [Line2D([0], [0], color=color, label=model_label(*model)) for model, color in zip(TOPOLOGIES, COLORS)]
    if native and not ratio:
        legend += [Line2D([0], [0], color="#333333", linestyle=style, label=label)
                   for style, label in (("-", "Native Asio callback"), ("--", "Native Asio coroutine"))]
    if any(belongs(row, name) and row["handler_work"] == work for row in failed) and not ratio:
        legend.append(Line2D([0], [0], color="#b33e4a", marker="x", linestyle="none", label="Failed profile; excluded"))
    title = "Native Asio TCP" if native else report.PROTOCOL_LABELS[name]
    if cpu and any(belongs(row, name) and row["handler_work"] == work and row.get("cpu_percent_estimated") for row in rows):
        title += " | CPU estimated"
    fig.suptitle(f"{title} | 16 clients | {work_label(work)}\nMedian; error bars = run min/max, not confidence intervals", fontsize=13, y=.99)
    fig.legend(handles=legend, loc="upper center", bbox_to_anchor=(.5, .945), ncol=3, fontsize=10)
    fig.subplots_adjust(top=.86, bottom=.06, left=.10, right=.98, hspace=.43, wspace=.16)
    fig.savefig(output, format="svg")
    plt.close(fig)


def failure_table(rows, chinese):
    if not rows:
        return "本协议没有失败配置。" if chinese else "No failed profiles for this protocol."
    lines = ["| " + ("后端 / 模式 | 消息字节 / 窗口 | 线程模型 | 业务计算 | 失败信息 |" if chinese else
                      "Backend / execution | Payload bytes / window | Thread model | Application computation | Failure |"),
             "| --- | --- | --- | --- | --- |"]
    for row in rows:
        errors = []
        for failure in row["failures"]:
            counters = ", ".join(f"{COUNTER_LABELS[key][int(chinese)]}: {value}" for key, value in failure["counters"].items())
            errors.append(f"#{failure['repetition']}: {failure['error']}" + (f"; {counters}" if counters else ""))
        execution = ("协程" if chinese else "coroutine") if row["implementation"] == "asio-coroutine" else ("回调" if chinese else "callback")
        cells = [f"{report.backend_name(row['backend'])} / {execution}",
                 f"{row['payload_bytes']} / {row['window']}", model_label(row["io_model"], row["io_threads"], chinese),
                 work_label(row["handler_work"], chinese),
                 "; ".join(errors)]
        lines.append("| " + " | ".join(str(cell).replace("|", "\\|").replace("\n", " ") for cell in cells) + " |")
    return "\n".join(lines)


def findings(rows, ratios, name, work, chinese):
    lines = []
    for backend in BACKENDS:
        if name == "native-tcp":
            values = [row["coroutine_callback_ratio"]["median"] for row in ratios
                      if row["backend"] == backend and row["handler_work"] == work]
            if values:
                lines.append(f"- {report.backend_name(backend)}: " +
                             (f"匹配配置的协程／回调吞吐倍率中位数范围为 {min(values):.2f}–{max(values):.2f}。" if chinese else
                              f"Matched coroutine/callback throughput medians range from {min(values):.2f}× to {max(values):.2f}×."))
            cpu_values = [row["coroutine_callback_cpu_ratio"]["median"] for row in ratios
                          if row["backend"] == backend and row["handler_work"] == work
                          and row.get("coroutine_callback_cpu_ratio") is not None]
            if cpu_values:
                lines.append(f"- {report.backend_name(backend)}: " +
                             (f"匹配配置的协程／回调 CPU 占用倍率中位数范围为 {min(cpu_values):.2f}–{max(cpu_values):.2f}；小于 1 表示更低的平均进程 CPU 占用，需结合吞吐倍率判断。" if chinese else
                              f"Matched coroutine/callback CPU medians range from {min(cpu_values):.2f}× to {max(cpu_values):.2f}×; below 1 means lower average process CPU. Compare with the throughput ratio."))
            continue
        values = [row for row in rows if belongs(row, name) and row["backend"] == backend
                  and row["payload_bytes"] == 1024 and row["handler_work"] == work]
        if not values:
            continue
        throughput = max(values, key=lambda row: row["roundtrips_per_second"]["median"])
        latency = min(values, key=lambda row: row["rtt_p99_us"]["median"])
        high = model_label(throughput["io_model"], throughput["io_threads"], chinese)
        low = model_label(latency["io_model"], latency["io_threads"], chinese)
        lines.append(f"- {report.backend_name(backend)}: " +
                     (f"1024 字节时，吞吐量中位数最高为 {high}、窗口 {throughput['window']}；p99 中位数最低为 {low}、窗口 {latency['window']}。" if chinese else
                      f"At 1024 bytes, the highest throughput median uses {high}, window {throughput['window']}; the lowest p99 median uses {low}, window {latency['window']}."))
    return "\n".join(lines)


def asset_prefix(name, work):
    return f"dimensions-{name}" + (f"-iterations{work}" if name == "native-tcp" else "")


def detail_page(name, rows, failed, ratios, chinese, logical_cpus=None):
    suffix = "_CN" if chinese else ""
    native = name == "native-tcp"
    title = ("原生 Asio TCP：回调与协程" if chinese else "Native Asio TCP: Callbacks and Coroutines") if native else report.PROTOCOL_LABELS[name]
    lines = [f"# {title}" + (" 负载对比" if chinese else " Load Comparison"), "",
             "16 个客户端；消息为 64/1024/16384 字节，窗口为 1/4/16/64；每个配置重复三次。" if chinese else
             "16 clients; 64/1024/16384-byte messages; windows 1/4/16/64; three repetitions per configuration.", ""]
    if native:
        lines += ["回调与协程都使用原生 Asio 和固定长度 TCP 批次：服务端完整读取一批，逐条执行相同计算后整批回写。窗口为 1 时记录单消息 RTT，更大窗口记录批次 RTT。arknet 公开协程端点 API 仍为 TODO。" if chinese else
                  "Both executions use native Asio and fixed-size TCP batches: the server reads a complete batch, applies the same computation to each message, then writes the entire reply. Window 1 records message RTT; larger windows record batch RTT. The public arknet coroutine endpoint API remains TODO.", "",
                  report.cpu_note(chinese, logical_cpus), ""]
    for work in ((0, 10000) if native else (0,)):
        prefix = asset_prefix(name, work)
        lines += ["## " + work_label(work, chinese), ""]
        labels = ("往返吞吐量", "双向应用负载吞吐量", "批次 p99 RTT" if native else "单消息 p99 RTT") if chinese else (
            "Round-trip throughput", "Bidirectional application payload throughput", "Batch p99 RTT" if native else "Message p99 RTT")
        for (_, metric_file), label in zip(METRIC_FILES, labels):
            lines += [f"![{label}](assets/{prefix}-{metric_file}.svg)", ""]
        if native:
            lines += [f"![{'平均进程 CPU 占用率' if chinese else 'Average process CPU utilization'}](assets/{prefix}-cpu.svg)", "",
                      f"![{'协程／回调吞吐倍率' if chinese else 'Coroutine/callback throughput ratio'}](assets/{prefix}-ratio.svg)", "",
                      f"![{'协程／回调 CPU 占用倍率' if chinese else 'Coroutine/callback CPU utilization ratio'}](assets/{prefix}-cpu-ratio.svg)", ""]
        lines += [findings(rows, ratios, name, work, chinese), ""]
    subset = [row for row in failed if belongs(row, name)]
    lines += [f"[{'完整数值 JSON（含 p95、运行范围与失败记录）' if chinese else 'Complete numeric JSON (p95, run ranges and failures)'}](dimensions-summary.json ':ignore')", "",
              "## " + ("失败配置" if chinese else "Failed Profiles"), "", failure_table(subset, chinese), "",
              "任一次重复失败，该配置的三次测量均不进入吞吐量、延迟或匹配倍率统计；原始数据保留失败记录。" if chinese else
              "If any repetition fails, all three measurements for that profile are excluded from rate, latency and matched-ratio summaries; raw failures are retained.", ""]
    if native:
        lines += ["吞吐和 CPU 倍率按相同后端、消息、批次大小、线程模型和计算量逐次配对后取中位数；1 表示相同。CPU 占用更低不一定处理更多请求，需结合吞吐倍率判断。CPU 缺少任一次采样时，CPU 统计和对应倍率为 N/A，吞吐统计仍可保留。该结果不能推算 arknet 回调端点与未来协程 API 的速度差异。" if chinese else
                  "Throughput and CPU ratios pair repetitions at the same backend, payload, batch size, thread model and computation before taking the median; 1 means equal values. Lower CPU utilization does not necessarily complete more requests; compare throughput too. If any CPU sample is missing, its CPU summary and matched ratio are N/A while throughput may remain valid. These results do not predict arknet callback endpoint versus future coroutine API speed.", ""]
    elif name == "udp":
        lines += ["UDP 窗口增加可能导致丢失；出现丢失的配置属于失败测量，不能作为吞吐能力。服务端仍通过一个 socket 串行接收。" if chinese else
                  "Larger UDP windows can lose traffic; profiles with loss are failed measurements, not capacity results. The server still receives through one serialized socket.", ""]
    lines += [f"[{'指标、统计口径与复现' if chinese else 'Metrics, statistics and reproduction'}](../testing{suffix}.md)", ""]
    return "\n".join(lines)


def raw_link(path, raw_base):
    results = Path(__file__).resolve().parent / "results"
    try:
        relative = path.resolve().relative_to(results).as_posix()
    except ValueError:
        relative = path.name
    return raw_base.rstrip("/") + "/" + quote(relative, safe="/")


def index_page(reports, rows, failed, chinese, raw_base, host_description=""):
    suffix = "_CN" if chinese else ""
    first = reports[0][1]
    settings = first["settings"]
    family = {"ipv4": "IPv4", "ipv6": "IPv6"}[report.validate_address_families(reports)]
    lines = ["# " + ("包体、批量与执行方式对比" if chinese else "Payload, Batch and Execution Comparisons"), "",
             f"本机 Release {family} 回环测试：16 个客户端，三种消息大小、四种窗口及三种线程模型，每个配置重复三次。" if chinese else
             f"Local Release {family} loopback: 16 clients, three payload sizes, four windows and three thread models, with three repetitions per configuration.", "",
             report.host_table(first["host"], chinese), "",
             (f"测量 {settings['seconds']} 秒，预热 {settings['warmup']} 秒。图表为中位数及最小／最大值，误差线不是置信区间；p99 为各次运行采样分位数的中位数。" if chinese else
              f"Measurement {settings['seconds']} s, warmup {settings['warmup']} s. Charts show medians and min/max, not confidence intervals; p99 is the median of per-run sampled percentiles."), "",
             report.cpu_note(chinese, first["host"].get("logical_cpus")), "",
             f"- Source SHA-256: `{first['source_sha256']}`",
             (f"- 测量数：{sum(len(value['results']) for _, value in reports):,}；有效配置：{len(rows)}；失败配置：{len(failed)}。" if chinese else
              f"- Measurements: {sum(len(value['results']) for _, value in reports):,}; valid profiles: {len(rows)}; failed profiles: {len(failed)}."), "",
             host_description or (f"[{'工具链与构建' if chinese else 'Toolchain and build'}](overview{suffix}.md#{'工具链与构建' if chinese else 'toolchain-and-build'})"), "",
             "arknet 协议测试记录单消息 RTT；原生 Asio 的回调／协程匹配测试记录批次 RTT。后者的固定批次数据不能与前者直接组成协程加速比；公开协程端点 API 仍为 TODO。" if chinese else
             "The arknet protocol program measures message RTT; matched native Asio callback/coroutine executions measure batch RTT. Their fixed-batch results do not establish speedups over arknet endpoints; public coroutine endpoint APIs remain TODO.", ""]
    for name in NAMES:
        title = ("原生 Asio TCP" if chinese else "Native TCP") if name == "native-tcp" else name.upper() if name == "tcps" else report.PROTOCOL_LABELS[name]
        lines += [f'<a id="{name}"></a>', "", f"## {title}", "",
                  f"[{'图表、简短结论与失败记录' if chinese else 'Charts, findings and failed profiles'}](dimensions-{name}{suffix}.md)", ""]
    lines += ["## " + ("原始数据" if chinese else "Raw Data"), "",
              f"[{'完整数值 JSON' if chinese else 'Complete numeric JSON'}](dimensions-summary.json ':ignore')", ""]
    for path, value in reports:
        lines.append(f"- [{path.name}]({raw_link(path, raw_base)}): {len(value['results'])} " +
                     ("次测量" if chinese else "measurements") +
                     f"; executable SHA-256 `{value.get('executable_sha256', 'not recorded')}`")
    lines += ["", f"[{'复现命令与指标定义' if chinese else 'Reproduction and metric definitions'}](../testing{suffix}.md)", "",
              "本机回环、固定连接分布和短时测量不代表生产容量，业务选型仍需按实际消息与连接分布复测。" if chinese else
              "Local loopback, fixed connection distributions and short runs do not establish production capacity; repeat with real messages and connection activity.", ""]
    return "\n".join(lines)


def generate(reports, groups, failed, output, raw_base, host_description=""):
    logical_cpus = reports[0][1]["host"].get("logical_cpus")
    rows = report.summary(groups, logical_cpus)
    ratios = paired_ratios(groups)
    output.mkdir(parents=True, exist_ok=True)
    assets = output / "assets"
    assets.mkdir(exist_ok=True)
    summary = {"source_sha256": reports[0][1]["source_sha256"], "host": reports[0][1]["host"],
               "measurements": sum(len(value["results"]) for _, value in reports),
               "profiles": rows, "failed_profiles": failed, "matched_ratios": ratios}
    (output / "dimensions-summary.json").write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    for name in NAMES:
        for work in ((0, 10000) if name == "native-tcp" else (0,)):
            prefix = asset_prefix(name, work)
            for metric, metric_file in METRIC_FILES:
                chart(rows, failed, assets / f"{prefix}-{metric_file}.svg", name, metric, work)
            if name == "native-tcp":
                chart(rows, failed, assets / f"{prefix}-cpu.svg", name, "cpu_percent", work)
                chart(ratios, [], assets / f"{prefix}-ratio.svg", name, "coroutine_callback_ratio", work)
                chart(ratios, [], assets / f"{prefix}-cpu-ratio.svg", name, "coroutine_callback_cpu_ratio", work)
        for chinese in (False, True):
            suffix = "_CN" if chinese else ""
            (output / f"dimensions-{name}{suffix}.md").write_text(detail_page(name, rows, failed, ratios, chinese, logical_cpus), encoding="utf-8")
    for chinese in (False, True):
        suffix = "_CN" if chinese else ""
        (output / f"dimensions{suffix}.md").write_text(index_page(reports, rows, failed, chinese, raw_base, host_description), encoding="utf-8")
    return rows, ratios


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reports", nargs="+", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--raw-base", default="https://raw.githubusercontent.com/OpenArkStudio/arknet/main/benchmarks/results")
    parser.add_argument("--host-description", default="", help="optional collected toolchain/build details")
    args = parser.parse_args()
    try:
        reports, groups, failed = load_reports(args.reports)
        rows, ratios = generate(reports, groups, failed, args.output_dir, args.raw_base, args.host_description)
    except (ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(f"Generated {len(rows)} valid profiles, {len(failed)} failed profiles and {len(ratios)} matched ratios in {args.output_dir}")


if __name__ == "__main__":
    main()
