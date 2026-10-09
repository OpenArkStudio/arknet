#!/usr/bin/env python3
"""Run every extracted client/server pair and reap its processes."""

import argparse
import json
from pathlib import Path
import selectors
import subprocess
import sys


def text(value):
    return value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or ""


def run_pair(protocol, build, root, logs):
    result = {"protocol": protocol, "ok": False}
    server_output = ""
    server_error = ""
    client_output = ""
    client_error = ""
    server = None
    try:
        server = subprocess.Popen([str(build / f"usage_{protocol}_server")],
                                  cwd=root, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True)
        result["server_pid"] = server.pid
        with selectors.DefaultSelector() as selector:
            selector.register(server.stdout, selectors.EVENT_READ)
            if not selector.select(timeout=10):
                raise RuntimeError("server did not announce readiness within ten seconds")
            server_output = server.stdout.readline()
        if "listening on" not in server_output:
            raise RuntimeError("server exited before announcing readiness")
        client = subprocess.run([str(build / f"usage_{protocol}_client")], cwd=root,
                                capture_output=True, text=True, timeout=30)
        client_output, client_error = client.stdout, client.stderr
        result["client_returncode"] = client.returncode
        if client.returncode != 0 or client_output.strip() != "echo received":
            raise RuntimeError("client failed its echoed-content check")
        result["ok"] = True
    except subprocess.TimeoutExpired as error:
        client_output, client_error = text(error.stdout), text(error.stderr)
        result["error"] = "client exceeded thirty-second process timeout"
    except (OSError, RuntimeError) as error:
        result["error"] = str(error)
    finally:
        if server is not None:
            try:
                output, server_error = server.communicate(input="\n", timeout=20)
                server_output += output
            except subprocess.TimeoutExpired:
                server.kill()
                output, server_error = server.communicate()
                server_output += output
                result["ok"] = False
                result["error"] = "server did not exit after Enter within twenty seconds"
            result["server_returncode"] = server.returncode
            result["server_reaped"] = server.poll() is not None
            if server.returncode != 0:
                result["ok"] = False
                result.setdefault("error", "server exited with a nonzero status")
        for role, output, error in (("server", server_output, server_error),
                                    ("client", client_output, client_error)):
            (logs / f"{protocol}-{role}.log").write_text(
                f"stdout:\n{output}\nstderr:\n{error}\n", encoding="utf-8")
    print(f"{protocol}: {'PASS' if result['ok'] else 'FAIL'}", flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", required=True, type=Path)
    parser.add_argument("--arknet", type=Path, default=Path("/Users/nick/work/opensource/arknet"))
    parser.add_argument("--logs", required=True, type=Path)
    args = parser.parse_args()
    args.logs.mkdir(parents=True, exist_ok=True)
    results = [run_pair(protocol, args.build.resolve(), args.arknet.resolve(), args.logs)
               for protocol in ("tcp", "udp", "websocket", "tls", "http", "https", "wss")]
    complete = all(result["ok"] and result.get("server_reaped") for result in results)
    (args.logs / "summary.json").write_text(
        json.dumps({"ok": complete, "results": results}, indent=2) + "\n", encoding="utf-8")
    return 0 if complete else 1


if __name__ == "__main__":
    sys.exit(main())
