#!/usr/bin/env python3
"""Extract synchronized Usage examples without configuring or compiling them."""

import argparse
from pathlib import Path
import re


def replace_once(source, old, new):
    if source.count(old) != 1:
        raise ValueError(f"expected exactly one occurrence of {old!r}")
    return source.replace(old, new, 1)


def indented(block):
    return "\n".join("    " + line if line else "" for line in block.rstrip().splitlines())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arknet", type=Path, default=Path("/Users/nick/work/opensource/arknet"))
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent / "project")
    args = parser.parse_args()
    fence = re.compile(r"^```cpp[ \t]*\n(.*?)^```[ \t]*$", re.MULTILINE | re.DOTALL)
    blocks = {}
    programs = {}
    for protocol in ("tcp", "udp", "websocket", "tls", "http"):
        folder = args.arknet / "docs" / protocol
        english = fence.findall((folder / "usage.md").read_text(encoding="utf-8"))
        chinese = fence.findall((folder / "usage_CN.md").read_text(encoding="utf-8"))
        if english != chinese:
            raise ValueError(f"{protocol}: English and Chinese C++ blocks differ")
        if len(english) < 2 or any("int main(" not in block for block in english[:2]):
            raise ValueError(f"{protocol}: separate runnable server and client required")
        blocks[protocol] = english
        for role, block in zip(("server", "client"), english[:2]):
            programs[f"{protocol}_{role}"] = block

    if len(blocks["http"]) != 4:
        raise ValueError("HTTP: expected two programs and two HTTPS replacement blocks")
    for role, supplemental in zip(("server", "client"), blocks["http"][2:]):
        source = programs[f"http_{role}"]
        source = replace_once(source, f"<arknet/http/http_{role}.hpp>", f"<arknet/http/https_{role}.hpp>")
        source = replace_once(source, f"    arknet::http_{role} {role};", indented(supplemental))
        host = "127.0.0.1" if role == "server" else "localhost"
        source = replace_once(source, f'{role}.start("127.0.0.1", 8080)', f'{role}.start("{host}", 8443)')
        if role == "server":
            source = replace_once(source, "http://127.0.0.1:8080/echo", "https://localhost:8443/echo")
        else:
            source = replace_once(source, '"127.0.0.1:8080"', '"localhost:8443"')
        programs[f"https_{role}"] = source

    for role in ("server", "client"):
        source = programs[f"websocket_{role}"]
        source = replace_once(source, f"<arknet/websocket/ws_{role}.hpp>", f"<arknet/websocket/wss_{role}.hpp>")
        supplemental = blocks["http"][2 if role == "server" else 3].replace("arknet::https_", "arknet::wss_")
        source = replace_once(source, f"    arknet::ws_{role} {role};", indented(supplemental))
        if role == "client":
            source = replace_once(source, 'client.start("127.0.0.1", 7001, "/echo")', 'client.start("localhost", 7001, "/echo")')
        else:
            source = replace_once(source, "ws://127.0.0.1:7001/echo", "wss://localhost:7001/echo")
        programs[f"wss_{role}"] = source

    args.output.mkdir(parents=True, exist_ok=True)
    cmake = [
        "cmake_minimum_required(VERSION 3.21)",
        "project(arknet_usage_examples LANGUAGES CXX)",
        'set(ARKNET_SOURCE_DIR "' + args.arknet.resolve().as_posix() + '" CACHE PATH "arknet checkout")',
        'option(ARKNET_ENABLE_SSL "Compile secure examples" ON)',
        "set(ARKNET_BUILD_TESTS OFF CACHE BOOL \"\" FORCE)",
        "set(ARKNET_BUILD_EXAMPLES OFF CACHE BOOL \"\" FORCE)",
        "set(ARKNET_BUILD_BENCHMARKS OFF CACHE BOOL \"\" FORCE)",
        'add_subdirectory("${ARKNET_SOURCE_DIR}" arknet)',
    ]
    for name, source in programs.items():
        (args.output / f"{name}.cpp").write_text(source, encoding="utf-8")
        secure = name.startswith(("tls_", "https_", "wss_"))
        if secure:
            cmake.append("if(ARKNET_ENABLE_SSL)")
        cmake.append(f"add_executable(usage_{name} {name}.cpp)")
        cmake.append(f"target_link_libraries(usage_{name} PRIVATE arknet::arknet)")
        if secure:
            cmake.append("endif()")
    (args.output / "CMakeLists.txt").write_text("\n".join(cmake) + "\n", encoding="utf-8")
    print(f"Prepared {len(programs)} examples in {args.output}; bilingual C++ blocks match.")
    print("No configure, compilation or network test was run.")


if __name__ == "__main__":
    main()
