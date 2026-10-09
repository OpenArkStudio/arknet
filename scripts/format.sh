#!/usr/bin/env bash
set -euo pipefail

case "${1:---check}" in
    --check) options=(--dry-run --Werror) ;;
    --fix) options=(-i) ;;
    *) printf 'Usage: bash scripts/format.sh [--check|--fix]\n' >&2; exit 2 ;;
esac
if [[ $# -gt 1 ]]; then
    printf 'Usage: bash scripts/format.sh [--check|--fix]\n' >&2
    exit 2
fi

formatter=${CLANG_FORMAT:-clang-format}
if ! command -v "$formatter" >/dev/null 2>&1; then
    printf 'clang-format 18 is required. Set CLANG_FORMAT to its executable.\n' >&2
    exit 2
fi
if [[ $("$formatter" --version) != *'version 18.'* ]]; then
    printf 'Use clang-format 18 to match the CI formatting rules.\n' >&2
    exit 2
fi

cd "$(dirname "${BASH_SOURCE[0]}")/.."
files=()
while IFS= read -r -d '' file; do
    case "$file" in
        include/arknet/bho/*|tests/vendor/*|tests/results/*|benchmarks/results/*) continue ;;
    esac
    case "$file" in
        *.cpp|*.hpp|*.h) files+=("$file") ;;
    esac
done < <(git ls-files -z --cached --others --exclude-standard -- include/arknet tests examples benchmarks)
if [[ ${#files[@]} -eq 0 ]]; then
    printf 'No project C++ files found.\n' >&2
    exit 2
fi
"$formatter" --style=file "${options[@]}" "${files[@]}"
printf 'Checked %s project C++ files.\n' "${#files[@]}"
