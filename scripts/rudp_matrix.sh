#!/usr/bin/env bash
# Maintainer collection tool; users can run arknet_rudp_benchmark directly.
set -euo pipefail

fail() { printf '%s\n' "$*" >&2; exit 2; }
usage() {
    cat <<'HELP'
Usage: rudp_matrix.sh --build-dir PATH --backend standalone|boost
                      --backend-include PATH --output DIRECTORY [options]
  --seconds NUMBER          Measured duration per round, default 1 (0 < value <= 600)
  --warmup NUMBER           Warmup per round, default 0.25 (0 <= value <= 600)
  --rounds COUNT            Default 3 (1..100)
  --clients COUNT           Default 8 (1..1024)
  --payloads VALUES...      Default 64 1024 4096 16384 (16..16384)
  --windows VALUES...       Default 1 4 16 (1..1024)
  --profiles VALUES...      Default shared:1 shared:4 sharded:4 (1..64 threads)
Output retains per-case JSON and backend metadata. Existing results are not overwritten.
Requires jq, shasum and GNU timeout (timeout or gtimeout); no Python is used.
HELP
}

build= backend= backend_include= output=
seconds=1 warmup=0.25 rounds=3 clients=8
payloads=(64 1024 4096 16384)
windows=(1 4 16)
profiles=(shared:1 shared:4 sharded:4)
while [ "$#" -gt 0 ]; do
    option=$1
    shift
    case "$option" in
        --help|-h) usage; exit 0 ;;
        --build-dir|--backend|--backend-include|--output|--seconds|--warmup|--rounds|--clients)
            [ "$#" -gt 0 ] || fail "$option requires a value"
            value=$1
            shift
            case "$option" in
                --build-dir) build=$value ;; --backend) backend=$value ;;
                --backend-include) backend_include=$value ;; --output) output=$value ;;
                --seconds) seconds=$value ;; --warmup) warmup=$value ;;
                --rounds) rounds=$value ;; --clients) clients=$value ;;
            esac ;;
        --payloads|--windows|--profiles)
            values=()
            while [ "$#" -gt 0 ]; do
                case "$1" in --*) break ;; esac
                values+=("$1")
                shift
            done
            [ "${#values[@]}" -gt 0 ] || fail "$option requires one or more values"
            case "$option" in
                --payloads) payloads=("${values[@]}") ;;
                --windows) windows=("${values[@]}") ;;
                --profiles) profiles=("${values[@]}") ;;
            esac ;;
        *) fail "unknown option: $option" ;;
    esac
done
[ -n "$build" ] && [ -n "$backend_include" ] && [ -n "$output" ] ||
    fail "--build-dir, --backend, --backend-include and --output are required"
case "$backend" in standalone|boost) ;; *) fail "--backend must be standalone or boost" ;; esac
for tool in jq shasum find sort awk; do
    command -v "$tool" >/dev/null 2>&1 || fail "$tool is required"
done
if command -v timeout >/dev/null 2>&1; then
    timeout_tool=$(command -v timeout)
elif command -v gtimeout >/dev/null 2>&1; then
    timeout_tool=$(command -v gtimeout)
else
    fail "GNU timeout or gtimeout is required"
fi
number() {
    jq -cn --arg value "$1" '$value |
        if test("^[+]?[0-9]+([.][0-9]+)?$") then tonumber else error("numeric argument required") end |
        if isfinite then . else error("finite number required") end'
}
seconds=$(number "$seconds") || fail "invalid seconds"
warmup=$(number "$warmup") || fail "invalid warmup"
rounds=$(number "$rounds") || fail "invalid rounds"
clients=$(number "$clients") || fail "invalid clients"
payload_json=$(jq -cn --args '$ARGS.positional | map(tonumber)' -- "${payloads[@]}") || fail "invalid payloads"
window_json=$(jq -cn --args '$ARGS.positional | map(tonumber)' -- "${windows[@]}") || fail "invalid windows"
profile_json=$(jq -cn --args '$ARGS.positional' -- "${profiles[@]}")
settings=$(jq -cn --argjson seconds "$seconds" --argjson warmup "$warmup" --argjson rounds "$rounds" \
    --argjson clients "$clients" --argjson payloads "$payload_json" --argjson windows "$window_json" \
    --argjson profiles "$profile_json" \
    '{seconds:$seconds,warmup:$warmup,rounds:$rounds,clients:$clients,payloads:$payloads,windows:$windows,profiles:$profiles}')
jq -en --argjson s "$settings" '
    $s.seconds > 0 and $s.seconds <= 600 and $s.warmup >= 0 and $s.warmup <= 600 and
    ($s.rounds | floor) == $s.rounds and $s.rounds >= 1 and $s.rounds <= 100 and
    ($s.clients | floor) == $s.clients and $s.clients >= 1 and $s.clients <= 1024 and
    all($s.payloads[]; isfinite and floor == . and . >= 16 and . <= 16384) and
    all($s.windows[]; isfinite and floor == . and . >= 1 and . <= 1024) and
    all([$s.payloads,$s.windows,$s.profiles][]; length == (unique | length)) and
    all($s.profiles[]; test("^(shared|sharded):([1-9]|[1-5][0-9]|6[0-4])$")) and
    $s.clients * ($s.payloads | max) * ($s.windows | max) <= 268435456' >/dev/null ||
    fail "duplicate, unsupported or out-of-range matrix dimension"

root=$(cd "$(dirname "$0")/.." && pwd -P)
[ -f "$build/CMakeCache.txt" ] || fail "CMakeCache.txt not found in build directory"
[ -d "$backend_include" ] || fail "backend include directory not found"
build=$(cd "$build" && pwd -P)
backend_include=$(cd "$backend_include" && pwd -P)
executable="$build/benchmarks/arknet_rudp_benchmark"
[ -x "$executable" ] || executable="$executable.exe"
[ -x "$executable" ] || fail "build arknet_rudp_benchmark first"
cache() { awk -v key="$1" 'index($0,key ":")==1 { print substr($0,index($0,"=")+1); exit }' "$build/CMakeCache.txt"; }
compiler=$(cache CMAKE_CXX_COMPILER)
[ -x "$compiler" ] || fail "CMake compiler is unavailable: $compiler"
build_type=$(cache CMAKE_BUILD_TYPE)
[ "$build_type" = Release ] || fail "performance matrix requires a Release build"
boost_enabled=$(cache ARKNET_USE_BOOST_ASIO)
case "$backend:$boost_enabled" in standalone:OFF|boost:ON) ;; *) fail "backend disagrees with CMake build" ;; esac
if [ "$backend" = standalone ]; then
    version_header="$backend_include/asio/version.hpp"
    version_macro=ASIO_VERSION
    configured_asio=$(cache ARKNET_ASIO_INCLUDE_DIR)
    [ -d "$configured_asio" ] || fail "CMake Asio include directory is unavailable"
    [ "$(cd "$configured_asio" && pwd -P)" = "$backend_include" ] || fail "Asio include directory disagrees with CMake build"
else
    version_header="$backend_include/boost/version.hpp"
    version_macro=BOOST_VERSION
fi
[ -f "$version_header" ] || fail "backend version header not found: $version_header"
version_number=$(awk -v name="$version_macro" '$1=="#define" && $2==name {print $3; exit}' "$version_header")
backend_version=$(jq -nr --argjson n "$version_number" '"\(($n / 100000) | floor).\((($n % 100000) / 100) | floor).\($n % 100)"')
compiler_version=$("$compiler" --version)
compiler_flags="$(cache CMAKE_CXX_FLAGS) $(cache CMAKE_CXX_FLAGS_RELEASE)"
mkdir -p "$output"
output=$(cd "$output" && pwd -P)
metadata="$output/metadata-$backend.json"
[ ! -e "$metadata" ] || fail "output already contains metadata for $backend: $output"
scratch=$(mktemp -d "${TMPDIR:-/tmp}/arknet-rudp-matrix.XXXXXX")
trap 'rm -rf "$scratch"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
timestamp() { date -u '+%Y-%m-%dT%H:%M:%SZ'; }
digest() { shasum -a 256 "$1" | awk '{print $1}'; }
source_hash() {
    (
        cd "$root"
        find include \( -type f -o -type l \) | LC_ALL=C sort
        printf '%s\n' benchmarks/rudp.cpp benchmarks/resources.hpp benchmarks/runtime.hpp
    ) > "$scratch/source-paths"
    while IFS= read -r relative; do
        printf '%s\0' "$relative"
        cat "$root/$relative"
        printf '\0'
    done < "$scratch/source-paths" | shasum -a 256 | awk '{print $1}'
}
source_digest=$(source_hash)
binary_digest=$(digest "$executable")
runner_digest=$(digest "$root/scripts/rudp_matrix.sh")
system=$(uname -s)
logical_cpus=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf null)
host=$(jq -cn --arg system "$system" --arg release "$(uname -r)" --arg architecture "$(uname -m)" \
    --argjson logical_cpus "$logical_cpus" '{system:$system,release:$release,architecture:$architecture,logical_cpus:$logical_cpus}')
if [ "$system" = Darwin ]; then
    for mapping in model:hw.model cpu:machdep.cpu.brand_string physical_cpus:hw.physicalcpu memory_bytes:hw.memsize; do
        value=$(sysctl -n "${mapping#*:}")
        host=$(jq -cn --argjson host "$host" --arg key "${mapping%%:*}" --arg value "$value" \
            '$host + {($key):(if $value | test("^[0-9]+$") then $value | tonumber else $value end)}')
    done
    host=$(jq -cn --argjson host "$host" --arg version "$(sw_vers -productVersion)" \
        --arg build "$(sw_vers -buildVersion)" '$host + {os_version:$version,os_build:$build}')
elif [ -r /proc/cpuinfo ]; then
    cpu=$(awk -F ': ' '/^model name/ {print $2; exit}' /proc/cpuinfo)
    memory=$(awk '/^MemTotal:/ {printf "%.0f",$2*1024}' /proc/meminfo)
    host=$(jq -cn --argjson host "$host" --arg cpu "$cpu" --argjson memory "$memory" \
        '$host + {cpu:$cpu,memory_bytes:$memory}')
fi
jq -n --arg backend "$backend" --arg backend_version "$backend_version" --arg compiler "$compiler" \
    --arg compiler_version "$compiler_version" --arg compiler_flags "$compiler_flags" --arg build "$build" \
    --arg executable "$executable" --arg source "$source_digest" --arg binary "$binary_digest" \
    --arg runner "$runner_digest" --arg start "$(timestamp)" --argjson host "$host" \
    --argjson settings "$settings" --arg git_head "$(git -C "$root" rev-parse HEAD)" \
    --arg git_status "$(git -C "$root" status --porcelain)" \
    '{schema_version:1,protocol:"rudp",wire_protocol:"kcp",api:"callback",backend:$backend,
      backend_version:$backend_version,compiler:$compiler,compiler_version:$compiler_version,
      compiler_flags:$compiler_flags,build_directory:$build,build_type:"Release",cxx_standard:20,
      executable:$executable,source_sha256:$source,binary_sha256:$binary,runner_sha256:$runner,
      git_head:$git_head,git_status:$git_status,started_at_utc:$start,host:$host,settings:$settings,
      complete:false,cases:[]}' > "$metadata"
limit=$(jq -nr --argjson s "$settings" '$s.rounds * ($s.seconds + $s.warmup + 25) + 30')
# jq accepts NaN/Infinity extensions; reject those tokens outside JSON strings.
strict_json() {
    awk '
        function flush() { if (token ~ /^[-+]?(NaN|Infinity)$/) bad=1; token="" }
        { for (i=1;i<=length($0);i++) {
            c=substr($0,i,1)
            if (quoted) { if (escaped) escaped=0; else if (c=="\\") escaped=1; else if (c=="\"") quoted=0 }
            else if (c=="\"") { flush(); quoted=1 }
            else if (c ~ /[A-Za-z0-9+.-]/) token=token c
            else flush()
          }; if (!quoted) flush() }
        END { flush(); exit bad ? 1 : 0 }' "$1" &&
        jq -e -s 'length == 1 and (.[0] | type == "object") and (.[0] | [.. | numbers] | all(isfinite))' "$1" >/dev/null
}
for payload in "${payloads[@]}"; do
    for window in "${windows[@]}"; do
        for profile in "${profiles[@]}"; do
            model=${profile%%:*}
            threads=${profile#*:}
            result="$output/${backend}-payload${payload}-window${window}-${model}${threads}.json"
            [ ! -e "$result" ] || fail "result already exists: $result"
            [ "$(digest "$executable")" = "$binary_digest" ] || fail "benchmark binary changed during collection"
            command=("$executable" --payload "$payload" --clients "$clients" --window "$window"
                     --io-model "$model" --io-threads "$threads" --seconds "$seconds" --warmup "$warmup" --rounds "$rounds")
            command_json=$(jq -cn --args '$ARGS.positional' -- "${command[@]}")
            started=$(timestamp)
            code=0
            "$timeout_tool" --kill-after=5 "$limit" "${command[@]}" > "$scratch/stdout" 2> "$scratch/stderr" || code=$?
            finished=$(timestamp)
            if ! strict_json "$scratch/stdout"; then
                jq -n --rawfile stdout "$scratch/stdout" '{ok:false,raw_stdout:$stdout}' > "$scratch/metrics"
            else
                cp "$scratch/stdout" "$scratch/metrics"
            fi
            jq --argjson command "$command_json" --arg started "$started" --arg finished "$finished" \
                --arg binary "$binary_digest" --arg source "$source_digest" --argjson code "$code" \
                --rawfile stderr "$scratch/stderr" \
                '. + {provenance:{command:$command,started_at_utc:$started,finished_at_utc:$finished,
                     binary_sha256:$binary,source_sha256:$source,returncode:$code,stderr:$stderr}}' \
                "$scratch/metrics" > "$result"
            jq --arg file "$(basename "$result")" '.cases += [$file]' "$metadata" > "$scratch/metadata"
            mv "$scratch/metadata" "$metadata"
            jq -e --arg backend "$backend" --arg model "$model" --argjson threads "$threads" \
                --argjson payload "$payload" --argjson clients "$clients" --argjson window "$window" \
                --argjson rounds "$rounds" --argjson seconds "$seconds" --argjson warmup "$warmup" '
                .ok == true and .provenance.returncode == 0 and .protocol == "rudp" and .wire_protocol == "kcp" and
                .api == "callback" and .backend == $backend and .io_model == $model and .io_threads == $threads and
                .payload_bytes == $payload and .clients == $clients and .window == $window and
                .round_count == $rounds and .requested_seconds == $seconds and .warmup_seconds == $warmup and
                .udp_receive_buffer_requested_bytes > 0 and
                (.rounds | length) == $rounds and .summary.successful_rounds == $rounds and .summary.failures == 0 and
                all(.rounds[]; .ok == true and .messages > 0 and .elapsed_seconds > 0 and .failures == 0 and
                    .udp_server_receive_buffer_bytes > 0 and .udp_client_receive_buffer_min_bytes > 0 and
                    .udp_client_receive_buffer_max_bytes >= .udp_client_receive_buffer_min_bytes and
                    .messages_sent == .messages and .server_received_messages == .messages and .pending_completions == 0)' \
                "$result" >/dev/null || fail "RUDP benchmark failed: $result"
            printf 'PASS %s\n' "$(basename "$result")"
        done
    done
done
[ "$(source_hash)" = "$source_digest" ] || fail "include/benchmark sources changed during collection"
[ "$(digest "$executable")" = "$binary_digest" ] || fail "benchmark binary changed during collection"
jq --arg end "$(timestamp)" '.complete=true | .finished_at_utc=$end' "$metadata" > "$scratch/metadata"
mv "$scratch/metadata" "$metadata"
