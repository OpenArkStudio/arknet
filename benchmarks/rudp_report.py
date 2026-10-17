#!/usr/bin/env python3
"""Create SVG charts and bilingual docs from completed local RUDP measurements."""

import argparse
import itertools
import json
import math
from pathlib import Path

from report import backend_name, model_name, run_statistics

FIELDS = ("roundtrips_per_second", "payload_roundtrip_mbit_per_second", "rtt_p50_us", "rtt_p99_us", "cpu_percent")
ARQ_FIELDS = ("arq_mtu_bytes", "arq_send_window_segments", "arq_receive_window_segments",
              "arq_update_interval_ms", "arq_congestion_control")
COLORS = ("#68737d", "#206db0", "#458f82")


def finite(value, positive=False):
    return type(value) in (int, float) and math.isfinite(value) and (value > 0 if positive else value >= 0)


def positive_integer(value):
    return type(value) is int and value > 0


def reject_constant(value):
    raise ValueError(f"non-finite JSON token: {value}")


def read_json(path):
    value = json.loads(path.read_text(encoding="utf-8"), parse_constant=reject_constant)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: JSON object required")
    return value


def load_results(directory):
    metadata, rows = [], []
    for backend in ("standalone", "boost"):
        meta = read_json(directory / f"metadata-{backend}.json")
        if (meta.get("complete") is not True or meta.get("backend") != backend or not meta.get("source_sha256")
                or meta.get("protocol") != "rudp" or meta.get("wire_protocol") != "kcp"
                or meta.get("api") != "callback" or meta.get("build_type") != "Release" or meta.get("cxx_standard") != 20):
            raise ValueError(f"{backend}: complete metadata required")
        settings = meta["settings"]
        if (not positive_integer(settings["rounds"]) or settings["rounds"] < 2
                or not positive_integer(settings["clients"]) or not finite(settings["seconds"], True)
                or not finite(settings["warmup"]) or not positive_integer(meta["host"].get("logical_cpus"))):
            raise ValueError("valid machine, durations and at least two measured rounds required")
        for field in ("payloads", "windows", "profiles"):
            values = settings[field]
            if not isinstance(values, list) or not values or len(values) != len(set(values)):
                raise ValueError(f"{field}: nonempty, unique dimensions required")
        if not all(positive_integer(value) for field in ("payloads", "windows") for value in settings[field]):
            raise ValueError("integer payloads and request windows required")
        profiles = []
        for profile in settings["profiles"]:
            parts = profile.split(":")
            if len(parts) != 2 or parts[0] not in ("shared", "sharded") or not parts[1].isdigit() or not 1 <= int(parts[1]) <= 64:
                raise ValueError("invalid IO model")
            profiles.append((parts[0], int(parts[1])))
        expected = {(payload, window, model, int(threads))
                    for payload, window, (model, threads) in itertools.product(settings["payloads"], settings["windows"], profiles)}
        seen = set()
        for name in meta["cases"]:
            if Path(name).name != name:
                raise ValueError("case filenames must stay inside the results directory")
            value = read_json(directory / name)
            identity = (value["payload_bytes"], value["window"], value["io_model"], value["io_threads"])
            provenance = value.get("provenance", {})
            if identity not in expected or identity in seen:
                raise ValueError(f"{name}: missing, duplicate or unexpected profile")
            seen.add(identity)
            if (value.get("ok") is not True or value.get("backend") != backend or value.get("protocol") != "rudp"
                    or value.get("wire_protocol") != "kcp" or value.get("api") != "callback"
                    or provenance.get("returncode") != 0 or provenance.get("source_sha256") != meta["source_sha256"]
                    or provenance.get("binary_sha256") != meta["binary_sha256"]
                    or value["clients"] != settings["clients"] or value["requested_seconds"] != settings["seconds"]
                    or value["warmup_seconds"] != settings["warmup"] or value["round_count"] != settings["rounds"]
                    or not positive_integer(value.get("udp_receive_buffer_requested_bytes"))
                    or value.get("cpu_percent_per_logical_core") != 100
                    or any(not positive_integer(value.get(field)) for field in ARQ_FIELDS[:-1])
                    or type(value.get("arq_congestion_control")) is not bool
                    or (value.get("peak_rss_bytes") is not None and not positive_integer(value["peak_rss_bytes"]))):
                raise ValueError(f"{name}: failed or incompatible measurement")
            rounds = value["rounds"]
            if len(rounds) != settings["rounds"] or [item["round"] for item in rounds] != list(range(1, len(rounds) + 1)):
                raise ValueError(f"{name}: incomplete or duplicate rounds")
            for item in rounds:
                if (item.get("ok") is not True or item.get("failures") != 0 or item.get("pending_completions") != 0
                        or not positive_integer(item.get("messages")) or item.get("messages_sent") != item["messages"]
                        or item.get("server_received_messages") != item["messages"]
                        or not finite(item.get("elapsed_seconds"), True)
                        or any(not finite(item.get(field)) for field in FIELDS)
                        or item["roundtrips_per_second"] <= 0 or item["rtt_p50_us"] > item["rtt_p99_us"]
                        or not positive_integer(item.get("udp_server_receive_buffer_bytes"))
                        or not positive_integer(item.get("udp_client_receive_buffer_min_bytes"))
                        or not positive_integer(item.get("udp_client_receive_buffer_max_bytes"))
                        or item["udp_client_receive_buffer_max_bytes"] < item["udp_client_receive_buffer_min_bytes"]):
                    raise ValueError(f"{name}: invalid metrics")
            row = {"backend": backend, "payload_bytes": identity[0], "window": identity[1],
                   "io_model": identity[2], "io_threads": identity[3], "raw_file": name,
                   "rounds": len(rounds), "messages": sum(item["messages"] for item in rounds),
                   "peak_rss_bytes": value["peak_rss_bytes"],
                   "requested_receive_buffer_bytes": value["udp_receive_buffer_requested_bytes"],
                   "server_receive_buffer_bytes": run_statistics([item["udp_server_receive_buffer_bytes"] for item in rounds]),
                   "client_receive_buffer_min_bytes": min(item["udp_client_receive_buffer_min_bytes"] for item in rounds),
                   "client_receive_buffer_max_bytes": max(item["udp_client_receive_buffer_max_bytes"] for item in rounds)}
            row.update({field: run_statistics([item[field] for item in rounds]) for field in FIELDS})
            row.update({field: value[field] for field in ARQ_FIELDS})
            row["cpu_machine_percent"] = {field: result / meta["host"]["logical_cpus"]
                                          for field, result in row["cpu_percent"].items()}
            rows.append(row)
        if seen != expected:
            raise ValueError(f"{backend}: incomplete matrix")
        metadata.append(meta)
    if (metadata[0]["source_sha256"] != metadata[1]["source_sha256"]
            or metadata[0]["host"] != metadata[1]["host"] or metadata[0]["settings"] != metadata[1]["settings"]
            or metadata[0]["compiler_version"] != metadata[1]["compiler_version"]
            or metadata[0]["compiler_flags"] != metadata[1]["compiler_flags"]):
        raise ValueError("hosts, sources, settings or compiler builds differ")
    if len({tuple(row[field] for field in ARQ_FIELDS) for row in rows}) != 1:
        raise ValueError("ARQ protocol settings differ")
    return metadata, rows


def chart(metadata, rows, output, field, title, unit, scale=1):
    import matplotlib
    matplotlib.use("Agg")
    matplotlib.rcParams["svg.fonttype"] = "none"
    import matplotlib.pyplot as plt
    from matplotlib.ticker import FuncFormatter, MaxNLocator

    settings = metadata[0]["settings"]
    payloads = settings["payloads"]
    panels = list(itertools.product(payloads, ("standalone", "boost")))
    fig, axes = plt.subplots(math.ceil(len(panels) / 2), 2, figsize=(10, 3.35 * math.ceil(len(panels) / 2)),
                             layout="constrained", squeeze=False, sharey="row")
    profiles = [profile.split(":") for profile in settings["profiles"]]
    legend = None
    for axis, (payload, backend) in zip(axes.flat, panels):
        for index, (model, threads) in enumerate(profiles):
            matching = [next(row for row in rows if row["backend"] == backend and row["payload_bytes"] == payload
                             and row["window"] == window and row["io_model"] == model and row["io_threads"] == int(threads))
                        for window in settings["windows"]]
            stats = [row[field] for row in matching]
            width = 0.75 / len(profiles)
            positions = [position - 0.375 + width / 2 + index * width for position in range(len(stats))]
            axis.bar(positions, [item["median"] / scale for item in stats], width=width,
                     yerr=([max(0, item["median"] - item["min"]) / scale for item in stats],
                           [max(0, item["max"] - item["median"]) / scale for item in stats]),
                     capsize=3, color=COLORS[index % len(COLORS)],
                     label=(f"1 io_context / {threads} thread" + ("s" if int(threads) > 1 else "")
                            if model == "shared" else f"{threads} io_contexts / 1 thread each"))
        axis.set_title(f"{backend_name(backend)} | {payload:,} B", fontsize=13)
        axis.set_xticks(range(len(settings["windows"])), settings["windows"], fontsize=11)
        axis.set_xlabel("In-flight requests per client", fontsize=11)
        axis.set_ylabel(unit, fontsize=11)
        axis.yaxis.set_major_locator(MaxNLocator(5))
        axis.yaxis.set_major_formatter(FuncFormatter(lambda value, _: f"{value:,.0f}" if value >= 100 else f"{value:g}"))
        axis.tick_params(axis="y", labelsize=10)
        axis.grid(axis="y", alpha=0.2)
        axis.set_axisbelow(True)
        axis.margins(y=0.2)
        legend = axis.get_legend_handles_labels()
    for axis in axes.flat[len(panels):]:
        axis.set_visible(False)
    fig.suptitle(f"RUDP {title} | {settings['clients']} clients | callback API\n"
                 f"Median; whiskers = min/max of {settings['rounds']} measured rounds", fontsize=14)
    fig.legend(*legend, loc="outside lower center", ncols=1, fontsize=12)
    fig.savefig(output, format="svg")
    plt.close(fig)
    output.write_text("\n".join(line.rstrip() for line in output.read_text(encoding="utf-8").splitlines()) + "\n", encoding="utf-8")


def documentation(metadata, rows, chinese, raw_directory, raw_url=None):
    settings = metadata[0]["settings"]
    host = metadata[0]["host"]
    cpu_count = host["logical_cpus"]
    server_min = min(row["server_receive_buffer_bytes"]["min"] for row in rows)
    server_max = max(row["server_receive_buffer_bytes"]["max"] for row in rows)
    client_min = min(row["client_receive_buffer_min_bytes"] for row in rows)
    client_max = max(row["client_receive_buffer_max_bytes"] for row in rows)
    requested = {row["requested_receive_buffer_bytes"] for row in rows}
    if len(requested) != 1:
        raise ValueError("receive-buffer requests differ")
    mib = lambda value: f"{value / 1048576:g}"
    compiler = metadata[0]["compiler_version"].splitlines()[0]
    versions = ", ".join(f"{backend_name(meta['backend'])} {meta['backend_version']}" for meta in metadata)
    focus_payload = 1024 if 1024 in settings["payloads"] else settings["payloads"][len(settings["payloads"]) // 2]
    model = host.get("model", host["system"])
    machine = f"Mac mini ({model})" if model == "Mac16,11" else model
    cores = f"{cpu_count} 个逻辑核心" if chinese else f"{cpu_count} logical CPUs"
    server_buffer = mib(server_min) if server_min == server_max else f"{mib(server_min)}–{mib(server_max)}"
    client_buffer = mib(client_min) if client_min == client_max else f"{mib(client_min)}–{mib(client_max)}"
    date_range = f"{min(meta['started_at_utc'] for meta in metadata)} / {max(meta['finished_at_utc'] for meta in metadata)}"
    arq = rows[0]
    arq_description = (f"MTU {arq['arq_mtu_bytes']} B；发送／接收窗口 {arq['arq_send_window_segments']}／{arq['arq_receive_window_segments']} 个分片；更新周期 {arq['arq_update_interval_ms']} ms；拥塞控制{'开启' if arq['arq_congestion_control'] else '关闭'}" if chinese else
                       f"MTU {arq['arq_mtu_bytes']} B; send / receive windows {arq['arq_send_window_segments']} / {arq['arq_receive_window_segments']} segments; timer {arq['arq_update_interval_ms']} ms; congestion control {'on' if arq['arq_congestion_control'] else 'off'}")
    os_name = (f"macOS {host['os_version']} ({host['os_build']})" if host["system"] == "Darwin"
               else f"{host['system']} {host['release']}")
    config = [("时间（UTC）" if chinese else "Time (UTC)", date_range),
              ("机器" if chinese else "Machine", f"{machine}, {host['cpu']}, {cores}, {host['memory_bytes'] / 1073741824:g} GiB RAM"),
              ("系统" if chinese else "OS", f"{os_name}, {host['architecture']}"),
              ("编译" if chinese else "Build", f"{compiler}; C++20 Release {metadata[0]['compiler_flags'].strip()}"),
              ("后端" if chinese else "Backends", versions),
              ("流量" if chinese else "Traffic", (f"{settings['clients']} 个客户端；IPv4 回环；回调 API" if chinese else f"{settings['clients']} clients; IPv4 loopback; callback API")),
              ("测量" if chinese else "Measurement", (f"每组 {settings['rounds']} 轮，每轮预热 {settings['warmup']} s、测量 {settings['seconds']} s" if chinese else f"{settings['rounds']} rounds per case; {settings['warmup']} s warmup and {settings['seconds']} s measured per round")),
              ("消息 / 在途请求" if chinese else "Payload / request window", f"{', '.join(map(str, settings['payloads']))} B / {', '.join(map(str, settings['windows']))}"),
              ("ARQ 参数" if chinese else "ARQ Settings", arq_description),
              ("接收缓冲" if chinese else "Receive buffers", (f"请求 {mib(next(iter(requested)))} MiB；服务端实际 {server_buffer} MiB，客户端实际 {client_buffer} MiB" if chinese else f"Requested {mib(next(iter(requested)))} MiB; effective server {server_buffer} MiB, clients {client_buffer} MiB"))]
    lines = ["# RUDP 性能" if chinese else "# RUDP Performance", "",
             (f"本机 {len(rows)} 组配置、{len(rows) * settings['rounds']} 次测量全部通过，共完成 {sum(row['messages'] for row in rows):,} 次消息往返；未发现消息丢失、内容错误、顺序错误或收尾超时。" if chinese else
              f"All {len(rows)} configurations and {len(rows) * settings['rounds']} measured rounds passed, completing {sum(row['messages'] for row in rows):,} message round trips with no missing messages, payload errors, ordering errors or drain timeouts."), "",
             "## 测试环境" if chinese else "## Test Environment", "", "| 配置 | 实际参数 |" if chinese else "| Item | Recorded Value |", "| --- | --- |"]
    lines += [f"| {key} | {value} |" for key, value in config]
    lines += ["", ("服务端全部会话共享一个 UDP socket 及其 strand，接收与 ARQ 处理串行执行。4 个 io_context 各 1 个线程的配置主要分散客户端，不能据此推断服务端已并行分片。" if chinese else
                   "All server sessions share one UDP socket and its strand; receive and ARQ processing remain serialized. The four-context configuration mainly distributes clients and does not demonstrate parallel server sharding."), "",
              ("应用消息按 ARQ MTU 分片；16 KiB 是消息大小，不是单个 UDP 数据报大小。" if chinese else
               "Application messages are fragmented according to the ARQ MTU; 16 KiB is a message size, not the size of one UDP datagram."), "",
              "## 往返吞吐量" if chinese else "## Round-Trip Throughput", "", "![RUDP round-trip throughput](assets/rudp-throughput.svg)", "",
              "## 往返延迟" if chinese else "## Round-Trip Latency", "", "![RUDP p99 RTT](assets/rudp-rtt.svg)", "",
              "## CPU 占用率" if chinese else "## CPU Utilization", "", "![RUDP CPU utilization](assets/rudp-cpu.svg)", "",
              (f"CPU 包含同一进程中的客户端、服务端及全部工作线程；单核占用为 100%，整机占比需除以本机 {cpu_count} 个逻辑核心。" if chinese else
               f"CPU covers the client, server and every worker in one process. One logical CPU is 100%; divide by this host's {cpu_count} logical CPUs to obtain the machine share."), "",
              f"## {focus_payload} B 数值" if chinese else f"## {focus_payload} B Results", "",
              ("| 后端 / IO 模型 | 每连接在途请求 | 往返 / s | 双向负载 Mbit/s | p50 RTT / ms | p99 RTT / ms | CPU / % |" if chinese else
               "| Backend / IO Model | In-Flight per Client | Round Trips / s | Payload Mbit/s, Both Directions | p50 RTT / ms | p99 RTT / ms | CPU / % |"),
              "| --- | ---: | ---: | ---: | ---: | ---: | ---: |"]
    for row in sorted((row for row in rows if row["payload_bytes"] == focus_payload),
                      key=lambda row: (row["backend"] != "standalone", row["window"], row["io_model"] == "sharded", row["io_threads"])):
        label = f"{backend_name(row['backend'])} / {model_name(row['io_model'], row['io_threads'], chinese)}"
        lines.append(f"| {label} | {row['window']} | {row['roundtrips_per_second']['median']:,.0f} | "
                     f"{row['payload_roundtrip_mbit_per_second']['median']:,.1f} | {row['rtt_p50_us']['median'] / 1000:.3f} | "
                     f"{row['rtt_p99_us']['median'] / 1000:.3f} | {row['cpu_percent']['median']:.1f} |")
    lines += ["", "## 结论与范围" if chinese else "## Findings and Scope", ""]
    for backend in ("standalone", "boost"):
        subset = [row for row in rows if row["backend"] == backend and row["payload_bytes"] == focus_payload]
        best = max(subset, key=lambda row: row["roundtrips_per_second"]["median"])
        low = min(subset, key=lambda row: row["rtt_p99_us"]["median"])
        lines.append((f"- {backend_name(backend)}：{focus_payload} B 吞吐最高为 {best['roundtrips_per_second']['median']:,.0f} 往返/s，CPU {best['cpu_percent']['median']:.1f}%（{model_name(best['io_model'], best['io_threads'], True)}，在途 {best['window']}）；最低 p99 为 {low['rtt_p99_us']['median'] / 1000:.3f} ms（{model_name(low['io_model'], low['io_threads'], True)}，在途 {low['window']}）。" if chinese else
                      f"- {backend_name(backend)}: highest {focus_payload} B throughput was {best['roundtrips_per_second']['median']:,.0f} round trips/s at {best['cpu_percent']['median']:.1f}% CPU ({model_name(best['io_model'], best['io_threads'])}, window {best['window']}); lowest p99 was {low['rtt_p99_us']['median'] / 1000:.3f} ms ({model_name(low['io_model'], low['io_threads'])}, window {low['window']})."))
    lines += ["", (f"柱高为 {settings['rounds']} 轮中位数，误差线为实际最小／最大值，不是置信区间。RTT 是各轮采样分位数的汇总，吞吐分母包含收完在途请求的时长；双向负载不含协议头。" if chinese else
                   "Bars show the median across rounds; whiskers are the actual minimum/maximum, not confidence intervals. RTT summarizes each round's sampled percentile; throughput includes in-flight drain time, and payload bandwidth excludes protocol headers."), "",
              (f"这是本机 IPv4 回环的 {settings['seconds']} s 短测范围，不代表 WAN 吞吐、延迟或抗丢包性能；本次没有测量协程 API。" if chinese else
               f"These are {settings['seconds']} s local IPv4 loopback measurements, not WAN throughput, latency or loss-resilience results. No coroutine API was measured."), "",
              "## 复现与原始数据" if chinese else "## Reproduction and Raw Data", "", "```sh",
              "cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release -DARKNET_BUILD_BENCHMARKS=ON \\",
              "  -DARKNET_USE_BOOST_ASIO=OFF -DARKNET_ASIO_INCLUDE_DIR=/path/to/asio/include",
              "cmake --build build-perf --target arknet_rudp_benchmark",
              "build-perf/benchmarks/arknet_rudp_benchmark --payload 1024 --clients 8 --window 16 \\",
              "  --io-model shared --io-threads 4 --seconds 1 --warmup 0.25 --rounds 3", "```", "",
              ("维护者批量采集使用 shell；单项 C++ 程序不需要 jq、Python 或 Ruby。" if chinese else
               "Maintainers collect the matrix with shell; the standalone C++ executable does not require jq, Python or Ruby."), "", "```sh",
              "bash scripts/rudp_matrix.sh --build-dir build-perf --backend standalone \\",
              "  --backend-include /path/to/asio/include --output /path/to/arknet-results/local-rudp", "```", "",
              ("Boost 在独立目录配置 `-DARKNET_USE_BOOST_ASIO=ON`；批量命令使用 `--backend boost --backend-include /path/to/boost/include`，其余测量参数相同。" if chinese else
               "Configure Boost in a separate build directory with `-DARKNET_USE_BOOST_ASIO=ON`; collect with `--backend boost --backend-include /path/to/boost/include` and identical measurement settings."), "",
              ("[完整数值与测量元数据](rudp-summary.json)" if chinese else "[Full values and measurement metadata](rudp-summary.json)"), "",
              (f"[{'原始 JSON、命令与机器信息' if chinese else 'Raw JSON, commands and host metadata'}]({raw_url})" if raw_url else
               (f"原始输入目录：`{raw_directory.name}`。发布归档后可用 `--raw-url` 指定链接。" if chinese else
                f"Raw input directory: `{raw_directory.name}`. Set `--raw-url` after publishing its archive.")), ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--raw-url", help="optional published URL for the raw input archive or directory")
    args = parser.parse_args()
    metadata, rows = load_results(args.results)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    assets = args.output_dir / "assets"
    assets.mkdir(exist_ok=True)
    chart(metadata, rows, assets / "rudp-throughput.svg", "roundtrips_per_second", "throughput", "Round trips / s")
    chart(metadata, rows, assets / "rudp-rtt.svg", "rtt_p99_us", "p99 latency", "p99 RTT (ms)", 1000)
    chart(metadata, rows, assets / "rudp-cpu.svg", "cpu_percent", "CPU utilization", "Process CPU (%)")
    for chinese, name in ((False, "rudp.md"), (True, "rudp_CN.md")):
        (args.output_dir / name).write_text(documentation(metadata, rows, chinese, args.results, args.raw_url), encoding="utf-8")
    (args.output_dir / "rudp-summary.json").write_text(json.dumps({"metadata": metadata, "results": rows}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
