#!/usr/bin/env python3
"""Run each loopback case in a fresh process and retain raw measurements."""

import argparse
import datetime
import hashlib
import json
from itertools import product
import math
import os
from pathlib import Path
import platform
import subprocess
import sys


def duration(value):
    number = float(value)
    if not math.isfinite(number) or number < 0 or number > 600:
        raise argparse.ArgumentTypeError("duration must be finite and between 0 and 600 seconds")
    return number


def git_value(root, *arguments):
    result = subprocess.run(["git", "-C", str(root), *arguments], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def host_information():
    info = {"system": platform.system(), "release": platform.release(),
            "machine": platform.machine(), "processor": platform.processor(),
            "logical_cpus": os.cpu_count()}
    if platform.system() == "Darwin":
        for field, key in (("model", "hw.model"), ("cpu", "machdep.cpu.brand_string"),
                           ("physical_cpus", "hw.physicalcpu"), ("memory_bytes", "hw.memsize")):
            value = subprocess.run(["sysctl", "-n", key], capture_output=True, text=True)
            if value.returncode == 0:
                raw = value.stdout.strip()
                info[field] = int(raw) if raw.isdecimal() else raw
        for field, option in (("os_version", "-productVersion"), ("os_build", "-buildVersion")):
            version = subprocess.run(["sw_vers", option], capture_output=True, text=True)
            if version.returncode == 0:
                info[field] = version.stdout.strip()
    return info


def source_hash(root):
    digest = hashlib.sha256()
    files = [root / "CMakeLists.txt", root / "vcpkg.json"]
    configuration = root / "vcpkg-configuration.json"
    if configuration.is_file():
        files.append(configuration)
    for directory in ("include", "cmake", "benchmarks", "ports"):
        files.extend(path for path in (root / directory).rglob("*")
                     if path.is_file() and path.suffix in (".hpp", ".h", ".ipp", ".cpp", ".py", ".cmake", ".in", ".txt", ".json")
                     and "results" not in path.relative_to(root).parts)
    for path in sorted(set(files)):
        digest.update(path.relative_to(root).as_posix().encode("utf-8") + b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def finite_json_number(value):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError(f"non-finite JSON number: {value}")
    return number


def validate_metrics(metrics, expected):
    if not isinstance(metrics, dict) or metrics.get("ok") is not True:
        raise RuntimeError("benchmark reported a failed measurement")
    for key in ("protocol", "payload_bytes", "clients", "window", "io_model", "io_threads", "handler_work"):
        if metrics.get(key) != expected[key]:
            raise RuntimeError(f"benchmark returned a different {key}")
    if metrics.get("messages", 0) <= 0 or metrics.get("elapsed_seconds", 0) <= 0:
        raise RuntimeError("benchmark did not measure completed traffic")
    if expected.get("execution") and metrics.get("implementation") != "asio-" + expected["execution"]:
        raise RuntimeError("benchmark returned a different execution mode")
    if expected.get("backend") and metrics.get("backend") != expected["backend"]:
        raise RuntimeError("benchmark returned a different backend")


def traffic_profiles(clients=None, windows=None):
    if clients is None and windows is None:
        return [{"clients": 1, "window": 1}, {"clients": 16, "window": 16}]
    return [{"clients": count, "window": window}
            for count in (clients or [16]) for window in (windows or [16])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--certs", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--backend", choices=["standalone", "boost"], help="verified backend label, retained on failures")
    parser.add_argument("--seconds", type=duration, default=3)
    parser.add_argument("--warmup", type=duration, default=1)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--protocols", nargs="+", choices=["tcp", "udp", "websocket", "tcps", "wss", "http", "https"],
                        default=["tcp", "udp", "websocket", "tcps", "wss", "http", "https"])
    parser.add_argument("--io-models", nargs="+", choices=["shared", "sharded"], default=["shared", "sharded"])
    parser.add_argument("--io-threads", nargs="+", type=int, default=[1, 2, 4])
    parser.add_argument("--work-values", nargs="+", type=int, default=[0])
    parser.add_argument("--payloads", nargs="+", type=int, default=[64, 1024, 16384])
    parser.add_argument("--clients", nargs="+", type=int, help="independent connection counts")
    parser.add_argument("--windows", nargs="+", type=int, help="outstanding requests per connection, including UDP")
    parser.add_argument("--execution-modes", nargs="+", choices=["callback", "coroutine"],
                        help="matched native TCP program only")
    parser.add_argument("--keep-going", action="store_true", help="retain failed load cases and finish the matrix")
    args = parser.parse_args()
    if args.seconds == 0 or not 1 <= args.repetitions <= 100:
        parser.error("seconds must be positive and repetitions must be between 1 and 100")
    if any(not 1 <= count <= 64 for count in args.io_threads) or any(not 0 <= work <= 10000000 for work in args.work_values):
        parser.error("IO threads must be between 1 and 64; handler work between 0 and 10000000")
    profiles = traffic_profiles(args.clients, args.windows)
    if any(not 16 <= payload <= 65507 for payload in args.payloads) or any(
            not 1 <= profile["clients"] <= 1024 or not 1 <= profile["window"] <= 1024 or
            profile["clients"] * profile["window"] * max(args.payloads) > 256 * 1024 * 1024
            for profile in profiles):
        parser.error("payloads must be 16..65507 bytes, clients/windows 1..1024, outstanding payloads <=256 MiB")
    for values in (args.protocols, args.payloads, args.clients, args.windows, args.io_threads, args.io_models,
                   args.work_values, args.execution_modes):
        if values is not None and len(values) != len(set(values)):
            parser.error("duplicate dimension values are not allowed")
    if args.execution_modes and (args.protocols != ["tcp"] or any(
            profile["clients"] > 64 or profile["window"] > 64 for profile in profiles)):
        parser.error("native execution comparison supports TCP and at most 64 clients/requests per batch")
    executable = args.executable.resolve(strict=True)
    certs = args.certs.resolve(strict=True)
    root = Path(__file__).resolve().parents[1]
    report = {
        "schema_version": 2,
        "backend": args.backend,
        "recorded_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "git_head": git_value(root, "rev-parse", "HEAD"),
        "git_status": git_value(root, "status", "--porcelain"),
        "source_sha256": source_hash(root),
        "executable_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "host": host_information(),
        "settings": {"seconds": args.seconds, "warmup": args.warmup,
                     "repetitions": args.repetitions, "payloads": args.payloads,
                     "profiles": profiles, "protocols": args.protocols,
                     "execution_modes": args.execution_modes,
                     "udp_throughput_window": 1 if args.windows is None else None,
                     "io_profiles": [{"io_model": model, "io_threads": threads, "handler_work": work}
                                     for model in args.io_models for threads in args.io_threads
                                     for work in args.work_values
                                     if not (model == "sharded" and threads == 1 and "shared" in args.io_models)]},
        "results": [],
        "complete": False,
        "failed_measurements": 0,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)

    def save():
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(args.output)

    save()
    cases = product(range(1, args.repetitions + 1), report["settings"]["io_profiles"],
                    args.protocols, args.payloads, profiles)
    for repetition, io_profile, protocol, payload, profile in cases:
        profile = dict(profile)
        if protocol == "udp" and args.windows is None:
            profile["window"] = 1
        executions = args.execution_modes or [None]
        if repetition % 2 == 0:
            executions = list(reversed(executions))
        for execution in executions:
            command = [str(executable), "--protocol", protocol, "--payload", str(payload),
                       "--clients", str(profile["clients"]), "--window", str(profile["window"]),
                       "--seconds", str(args.seconds), "--warmup", str(args.warmup),
                       "--certs", str(certs), "--io-model", io_profile["io_model"],
                       "--io-threads", str(io_profile["io_threads"]),
                       "--work", str(io_profile["handler_work"])]
            if execution:
                command.extend(["--execution", execution])
            result = {"repetition": repetition, "protocol": protocol, "payload_bytes": payload,
                      **profile, **io_profile, "command": command}
            if args.backend:
                result["backend"] = args.backend
            if execution:
                result["execution"] = execution
            report["results"].append(result)
            print(f"{repetition}/{args.repetitions} {protocol} {payload} B "
                  f"clients={profile['clients']} window={profile['window']} "
                  f"{io_profile['io_model']}:{io_profile['io_threads']} work={io_profile['handler_work']} "
                  f"{execution or 'arknet-callback'}", flush=True)
            try:
                completed = subprocess.run(command, capture_output=True, text=True,
                                           timeout=args.seconds + args.warmup + 45)
                result.update(returncode=completed.returncode, stderr=completed.stderr)
                try:
                    result["metrics"] = json.loads(completed.stdout, parse_float=finite_json_number,
                                                   parse_constant=finite_json_number)
                except ValueError as error:
                    result.update(stdout=completed.stdout, error=str(error))
                    raise RuntimeError("benchmark did not return valid JSON") from error
                if completed.returncode != 0:
                    raise RuntimeError(f"benchmark failed with exit code {completed.returncode}")
                validate_metrics(result["metrics"], result)
            except (subprocess.TimeoutExpired, OSError, RuntimeError) as error:
                result["error"] = str(error)
                report["failed_measurements"] += 1
                save()
                print(f"Failed: {error}. Report: {args.output}", file=sys.stderr)
                if not args.keep_going:
                    return 1
            save()
    report["complete"] = True
    save()
    print(f"Saved {len(report['results'])} measurements to {args.output}")
    return 1 if report["failed_measurements"] else 0


if __name__ == "__main__":
    sys.exit(main())
