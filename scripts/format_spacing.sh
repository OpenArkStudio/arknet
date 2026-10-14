#!/usr/bin/env bash
set -euo pipefail

case "${1:---check}" in
    --check|--fix) mode=$1 ;;
    *) printf 'Usage: bash scripts/format_spacing.sh [--check|--fix] [files...]
' >&2; exit 2 ;;
esac
shift

if [[ $# -eq 0 ]]; then
    while IFS= read -r -d '' file; do
        case "$file" in
            include/arknet/bho/*|include/arknet/thirdparty/*|tests/vendor/*|tests/results/*|benchmarks/results/*) continue ;;
        esac
        case "$file" in
            *.cpp|*.hpp|*.h) set -- "$@" "$file" ;;
        esac
    done < <(git ls-files -z --cached --others --exclude-standard -- include/arknet tests examples benchmarks)
fi

normalize() {
    perl -0pe \
        's/(^[ \t]*#include[^\n]*\n)(?![ \t]*#include)(?!\n)(?=.)/$1\n/gm; \
         s/(^[ \t]*namespace[^\n]*\{[ \t]*\n)(?!\n)(?=.)/$1\n/gm; \
         s/(^\};\n)(?!\n)(?=(?:template|class|struct|enum|namespace|inline|constexpr|using)\b)/$1\n/gm' "$1"
}

failed=0
for file in "$@"; do
    if [[ "$mode" == '--fix' ]]; then
        temporary=$(mktemp)
        normalize "$file" > "$temporary"
        mv "$temporary" "$file"
    else
        temporary=$(mktemp)
        normalize "$file" > "$temporary"
        if ! cmp -s "$file" "$temporary"; then
            printf '%s: required blank lines are missing\n' "$file" >&2
            failed=1
        fi
        rm -f "$temporary"
    fi
done

exit "$failed"
