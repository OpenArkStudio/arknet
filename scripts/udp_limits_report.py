#!/usr/bin/env python3
"""Render archived IPv4 UDP boundary diagnostics; never launch a network test."""

import argparse
from collections import defaultdict
from itertools import product
import json
from pathlib import Path
import re
import statistics


BACKENDS = ("standalone", "boost")
PAYLOADS = (1472, 8192, 16356, 16357, 16384, 65507)
WINDOWS = (1, 16, 64)
COUNTERS = ("client_send_rejections", "client_send_completion_errors",
            "server_send_rejections", "server_send_completion_errors",
            "content_errors", "order_errors", "unexpected_messages", "disconnect_errors",
            "udp_lost_messages", "unfinished_messages", "callback_exceptions",
            "drain_timeouts", "duplicate_completions", "pending_completions", "warmup_errors")
IP_FIELDS = ("fragment received", "reassembled ok", "output datagram fragmented", "fragment created")


def reject_constant(value):
    raise ValueError(f"non-finite JSON constant: {value}")


def kernel_counter(path, label):
    matches = re.findall(r"^\s*(\d+) " + re.escape(label) + r"\s*$", path.read_text(), re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f"{path}: expected one {label} counter")
    return int(matches[0])


def collect(folder):
    reports, groups, kernel = {}, [], {}
    expected = set(product(PAYLOADS, WINDOWS, (1, 2, 3)))
    for backend in BACKENDS:
        path = folder / f"{backend}.json"
        value = json.loads(path.read_text(encoding="utf-8"), parse_constant=reject_constant)
        settings = value.get("settings", {})
        if any(record.get("address_family", "ipv4") != "ipv4" for record in (value, settings)):
            raise ValueError(f"{path}: this diagnostic report requires IPv4 data")
        settings["address_family"] = "ipv4"
        if (value.get("schema_version") != 2 or value.get("complete") is not True
                or value.get("backend") != backend or not value.get("source_sha256")):
            raise ValueError(f"{path}: complete schema 2 report with backend/source required")
        if (settings.get("payloads") != list(PAYLOADS) or settings.get("repetitions") != 3
                or settings.get("protocols") != ["udp"]
                or settings.get("profiles") != [{"clients": 16, "window": window} for window in WINDOWS]
                or settings.get("io_profiles") != [{"io_model": "shared", "io_threads": 1, "handler_work": 0}]):
            raise ValueError(f"{path}: unexpected diagnostic load matrix")
        seen, profiles = set(), defaultdict(list)
        for entry in value.get("results", []):
            identity = tuple(entry.get(field) for field in ("payload_bytes", "window", "repetition"))
            if (any(type(item) is not int for item in identity)
                    or identity not in expected or identity in seen):
                raise ValueError(f"{path}: unexpected or duplicate diagnostic case")
            seen.add(identity)
            metric = entry.get("metrics")
            if not isinstance(metric, dict):
                raise ValueError(f"{path}: diagnostic case has no metrics")
            if any(record.get("address_family", "ipv4") != "ipv4" for record in (entry, metric)):
                raise ValueError(f"{path}: this diagnostic report requires IPv4 data")
            requested = dict(protocol="udp", clients=16, io_model="shared", io_threads=1,
                             handler_work=0, backend=backend, payload_bytes=identity[0], window=identity[1])
            if any(metric.get(field) != item or entry.get(field) != item for field, item in requested.items()):
                raise ValueError(f"{path}: metric/case parameters differ")
            if any(type(metric.get(field)) is not int or metric[field] < 0 for field in COUNTERS):
                raise ValueError(f"{path}: invalid diagnostic error counter")
            passed = (entry.get("returncode") == 0 and metric.get("ok") is True
                      and metric.get("warmup_ok") is True and not entry.get("error")
                      and not metric.get("error") and not any(metric[field] for field in COUNTERS))
            profiles[identity[:2]].append((identity[2], metric, passed))
        if seen != expected:
            raise ValueError(f"{path}: incomplete diagnostic matrix")
        failed = sum(not passed for samples in profiles.values() for _, _, passed in samples)
        if value.get("failed_measurements") != failed:
            raise ValueError(f"{path}: failure count disagrees with measurements")
        for (payload, window), samples in sorted(profiles.items()):
            samples.sort()
            losses = [metric["udp_lost_messages"] for _, metric, _ in samples]
            groups.append(dict(backend=backend, payload_bytes=payload, window=window,
                               passed_repetitions=sum(passed for _, _, passed in samples),
                               lost_echoes={"median": statistics.median(losses), "min": min(losses), "max": max(losses)},
                               repetitions=[dict(repetition=index, passed=passed,
                                                 counters={field: metric[field] for field in COUNTERS})
                                            for index, metric, passed in samples]))
        snapshots = {}
        for phase in ("before", "after"):
            snapshots[phase] = {"full_socket_buffer_drops": kernel_counter(
                folder / f"{backend}-{phase}-netstat-udp.txt", "dropped due to full socket buffers")}
            snapshots[phase].update({field: kernel_counter(folder / f"{backend}-{phase}-netstat-ip.txt", field)
                                     for field in IP_FIELDS})
        kernel[backend] = dict(snapshots, delta={field: snapshots["after"][field] - snapshots["before"][field]
                                               for field in snapshots["before"]})
        if any(delta < 0 for delta in kernel[backend]["delta"].values()):
            raise ValueError("kernel counters decreased between snapshots")
        reports[backend] = value
    if (len({value["source_sha256"] for value in reports.values()}) != 1
            or len({json.dumps(value["host"], sort_keys=True) for value in reports.values()}) != 1
            or len({json.dumps(value["settings"], sort_keys=True) for value in reports.values()}) != 1):
        raise ValueError("diagnostic backends have different sources, hosts or settings")
    return dict(address_family="ipv4", source_sha256=reports["standalone"]["source_sha256"],
                host=reports["standalone"]["host"], settings=reports["standalone"]["settings"],
                measurements=sum(len(value["results"]) for value in reports.values()),
                profiles=groups, kernel=kernel)


def render(summary, output):
    import matplotlib
    matplotlib.use("Agg")
    matplotlib.rcParams["svg.fonttype"] = "none"
    import matplotlib.pyplot as plt

    figure, axes = plt.subplots(2, 1, figsize=(10, 7.5), layout="constrained")
    maximum = max(row["lost_echoes"]["max"] for row in summary["profiles"])
    for backend, axis in zip(BACKENDS, axes):
        profiles = {(row["payload_bytes"], row["window"]): row for row in summary["profiles"]
                    if row["backend"] == backend}
        values = [[profiles[payload, window]["lost_echoes"]["median"] for payload in PAYLOADS] for window in WINDOWS]
        graphic = axis.pcolormesh([index - .5 for index in range(len(PAYLOADS) + 1)],
                                  [index - .5 for index in range(len(WINDOWS) + 1)],
                                  values, cmap="Reds", vmin=0, vmax=max(1, maximum),
                                  shading="flat", rasterized=False)
        axis.set_ylim(len(WINDOWS) - .5, -.5)
        for row_index, window in enumerate(WINDOWS):
            for column_index, payload in enumerate(PAYLOADS):
                profile = profiles[payload, window]
                loss = profile["lost_echoes"]["median"]
                passed = profile["passed_repetitions"]
                axis.text(column_index, row_index, f"{loss:,.0f}\n{passed}/3 passed",
                          ha="center", va="center", fontsize=11,
                          color="white" if loss > maximum * .55 else "#20252a")
        axis.set_title("Standalone Asio" if backend == "standalone" else "Boost.Asio", fontsize=13)
        axis.set_xticks(range(len(PAYLOADS)), [f"{payload:,}" for payload in PAYLOADS], fontsize=11)
        axis.set_yticks(range(len(WINDOWS)), [str(window) for window in WINDOWS], fontsize=11)
        axis.set_xlabel("UDP application payload (bytes)", fontsize=11)
        axis.set_ylabel("Outstanding requests\nper client", fontsize=11)
        axis.set_xticks([index - .5 for index in range(len(PAYLOADS) + 1)], minor=True)
        axis.set_yticks([index - .5 for index in range(len(WINDOWS) + 1)], minor=True)
        axis.grid(which="minor", color="white", linewidth=2)
        axis.tick_params(which="minor", bottom=False, left=False)
    settings = summary["settings"]
    figure.suptitle("IPv4 UDP loss on local loopback\n"
                    f"16 clients | 1 context / 1 thread | {settings['seconds']} s measurement + {settings['warmup']} s warmup",
                    fontsize=14)
    colorbar = figure.colorbar(graphic, ax=axes, shrink=.85, pad=.025)
    if colorbar.solids is not None:
        colorbar.solids.set_rasterized(False)
    colorbar.set_label("Measured-phase lost echoes per run (median of 3)", fontsize=11)
    output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output, format="svg")
    plt.close(figure)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        summary = collect(arguments.input_dir)
        render(summary, arguments.output)
        numeric = arguments.output.with_suffix(".json")
        numeric.write_text(json.dumps(summary, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    except (ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(f"Rendered {summary['measurements']} archived measurements to {arguments.output}")


if __name__ == "__main__":
    main()
