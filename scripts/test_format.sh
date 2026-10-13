#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT
cp "$root/.clang-format" "$root/.clang-format-ignore" "$temporary/"
mkdir -p "$temporary/scripts" "$temporary/include/arknet/bho" "$temporary/tests/vendor" \
    "$temporary/include/arknet/thirdparty" \
    "$temporary/tests/results" "$temporary/benchmarks/results" "$temporary/examples"
cp "$root/scripts/format.sh" "$temporary/scripts/"
cd "$temporary"
git init -q
printf 'struct probe{int value;};\n' > original.hpp
cp original.hpp include/arknet/probe.hpp
cp original.hpp 'examples/path with spaces.cpp'
for directory in include/arknet/bho include/arknet/thirdparty tests/vendor tests/results benchmarks/results; do
    cp original.hpp "$directory/ignored.hpp"
done
git add include/arknet/probe.hpp
printf '%s\n' '#include "check.hpp"' '#include <arknet/probe.hpp>' '#include <openssl/ssl.h>' \
    '#include <vector>' '#include <sys/socket.h>' '#include "vendor/doctest/doctest.h"' \
    '#include <arknet/bho/beast.hpp>' '#include <asio.hpp>' '#include <cstdint>' \
    '#include <psapi.h>' '#include <windows.h>' '#include <zlib.h>' > examples/probe.cpp
printf '%s\n' '#include <windows.h>' '#include <cstdint>' '#include <psapi.h>' \
    '#include <sys/socket.h>' '#include <vector>' '' '#include "vendor/doctest/doctest.h"' \
    '#include <arknet/bho/beast.hpp>' '#include <asio.hpp>' '#include <openssl/ssl.h>' \
    '#include <zlib.h>' '' '#include <arknet/probe.hpp>' '#include "check.hpp"' > expected-includes.cpp

expect_failure() {
    local expected=$1
    shift
    local actual=0
    "$@" > output.log 2>&1 || actual=$?
    if [[ $actual -ne $expected ]]; then
        printf 'Expected exit %s, got %s: %s\n' "$expected" "$actual" "$*" >&2
        exit 1
    fi
}
expect_failure 1 bash scripts/format.sh --check
expect_failure 2 bash scripts/format.sh --unknown
expect_failure 2 bash scripts/format.sh --check --extra
expect_failure 2 env CLANG_FORMAT=/missing/clang-format bash scripts/format.sh --check
expect_failure 2 env CLANG_FORMAT=true bash scripts/format.sh --check
bash scripts/format.sh --fix
bash scripts/format.sh --check
cmp expected-includes.cpp examples/probe.cpp
for directory in include/arknet/bho include/arknet/thirdparty tests/vendor tests/results benchmarks/results; do
    cmp original.hpp "$directory/ignored.hpp"
    "${CLANG_FORMAT:-clang-format}" --dry-run --Werror "$directory/ignored.hpp"
done
if cmp -s original.hpp include/arknet/probe.hpp || cmp -s original.hpp 'examples/path with spaces.cpp'; then
    printf 'The formatter did not update both tracked and untracked sources.\n' >&2
    exit 1
fi
printf 'Formatting runner checks passed.\n'
