#!/usr/bin/env bash
# Maintainer batch runner. The benchmark executables can also be run directly.
set -euo pipefail

fail() { printf '%s\n' "$*" >&2; exit 2; }
usage() {
    cat <<'HELP'
Usage: benchmark_matrix.sh --executable PATH --certs PATH --output PATH [options]
  --backend standalone|boost
  --address-family ipv4|ipv6  Loopback address family, default ipv4
  --seconds NUMBER          Measured duration, default 3 (0 < value <= 600)
  --warmup NUMBER           Warmup duration, default 1 (0 <= value <= 600)
  --repetitions COUNT       Default 3 (1..100)
  --protocols VALUES...     tcp udp websocket tcps wss http https
  --io-models VALUES...     shared sharded
  --io-threads VALUES...    Default 1 2 4 (1..64)
  --work-values VALUES...   Default 0 (0..10000000)
  --payloads VALUES...      Default 64 1024 16384 (16..65507)
  --clients VALUES...       Independent connection counts (1..1024)
  --windows VALUES...       Independent request windows, including UDP (1..1024)
  --execution-modes VALUES...  callback coroutine (matched native TCP only)
  --keep-going              Finish failed matrices and still exit nonzero
Requires jq, shasum and GNU timeout (timeout or gtimeout). No Python is used.
HELP
}

executable= certs= output= backend= address_family=ipv4
seconds=3 warmup=1 repetitions=3 keep_going=0
protocols='["tcp","udp","websocket","tcps","wss","http","https"]'
models='["shared","sharded"]' threads='[1,2,4]' works='[0]'
payloads='[64,1024,16384]' clients='[]' windows='[]' executions='[]'

while [ "$#" -gt 0 ]; do
    option=$1
    shift
    case "$option" in
        --help|-h) usage; exit 0 ;;
        --keep-going) keep_going=1 ;;
        --executable|--certs|--output|--backend|--address-family|--seconds|--warmup|--repetitions)
            [ "$#" -gt 0 ] || fail "$option requires a value"
            value=$1
            shift
            case "$option" in
                --executable) executable=$value ;; --certs) certs=$value ;;
                --output) output=$value ;; --backend) backend=$value ;;
                --address-family) address_family=$value ;;
                --seconds) seconds=$value ;; --warmup) warmup=$value ;;
                --repetitions) repetitions=$value ;;
            esac ;;
        --protocols|--io-models|--io-threads|--work-values|--payloads|--clients|--windows|--execution-modes)
            values=()
            while [ "$#" -gt 0 ]; do
                case "$1" in --*) break ;; esac
                values+=("$1")
                shift
            done
            [ "${#values[@]}" -gt 0 ] || fail "$option requires one or more values"
            command -v jq >/dev/null 2>&1 || fail "jq is required"
            case "$option" in
                --protocols|--io-models|--execution-modes)
                    value=$(jq -cn --args '$ARGS.positional' -- "${values[@]}") ;;
                *)
                    value=$(jq -cn --args '$ARGS.positional | map(
                        if test("^[+-]?[0-9]+$") then tonumber
                        else error("integer dimension required") end)' -- "${values[@]}") ||
                        fail "$option requires integers" ;;
            esac
            case "$option" in
                --protocols) protocols=$value ;; --io-models) models=$value ;;
                --io-threads) threads=$value ;; --work-values) works=$value ;;
                --payloads) payloads=$value ;; --clients) clients=$value ;;
                --windows) windows=$value ;; --execution-modes) executions=$value ;;
            esac ;;
        *) fail "unknown option: $option" ;;
    esac
done
[ -n "$executable" ] && [ -n "$certs" ] && [ -n "$output" ] ||
    fail "--executable, --certs and --output are required"
for tool in jq shasum find sort; do
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
        if test("^[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?$")
        then tonumber else error("finite number required") end |
        if isfinite then . else error("finite number required") end'
}
seconds=$(number "$seconds") || fail "invalid seconds"
warmup=$(number "$warmup") || fail "invalid warmup"
repetitions=$(number "$repetitions") || fail "invalid repetitions"
settings=$(jq -cn --argjson seconds "$seconds" --argjson warmup "$warmup" \
    --arg family "$address_family" \
    --argjson repetitions "$repetitions" --argjson payloads "$payloads" \
    --argjson clients "$clients" --argjson windows "$windows" \
    --argjson protocols "$protocols" --argjson models "$models" \
    --argjson threads "$threads" --argjson works "$works" --argjson executions "$executions" '
    {seconds:$seconds, warmup:$warmup, repetitions:$repetitions, payloads:$payloads, address_family:$family,
     protocols:$protocols, execution_modes:(if $executions == [] then null else $executions end),
     udp_throughput_window:(if $windows == [] then 1 else null end),
     profiles:(if $clients == [] and $windows == [] then
        [{clients:1,window:1},{clients:16,window:16}]
        else [(if $clients == [] then [16] else $clients end)[] as $c |
              (if $windows == [] then [16] else $windows end)[] as $w | {clients:$c,window:$w}] end),
     io_profiles:[$models[] as $m | $threads[] as $t | $works[] as $w |
        select($m != "sharded" or $t != 1 or ($models | index("shared")) == null) |
        {io_model:$m,io_threads:$t,handler_work:$w}]}')
jq -en --argjson settings "$settings" --arg backend "$backend" \
    --argjson dimensions "[$protocols,$models,$threads,$works,$payloads,$clients,$windows,$executions]" '
    $settings as $s |
    ($backend == "" or $backend == "standalone" or $backend == "boost") and
    ($s.address_family == "ipv4" or $s.address_family == "ipv6") and
    ($s.seconds > 0 and $s.seconds <= 600 and $s.warmup >= 0 and $s.warmup <= 600) and
    ($s.repetitions == ($s.repetitions | floor) and $s.repetitions >= 1 and $s.repetitions <= 100) and
    all($dimensions[]; length == (unique | length)) and
    all($s.protocols[]; . as $v | ["tcp","udp","websocket","tcps","wss","http","https"] | index($v) != null) and
    all($s.io_profiles[]; (.io_model == "shared" or .io_model == "sharded") and
        .io_threads >= 1 and .io_threads <= 64 and .handler_work >= 0 and .handler_work <= 10000000) and
    all($s.payloads[]; . >= 16 and . <= 65507) and
    all($s.profiles[]; .clients >= 1 and .clients <= 1024 and .window >= 1 and .window <= 1024 and
        .clients * .window * ($s.payloads | max) <= 268435456) and
    ($s.execution_modes == null or
        ($s.protocols == ["tcp"] and all($s.profiles[]; .clients <= 64 and .window <= 64) and
         all($s.execution_modes[]; . == "callback" or . == "coroutine")))' >/dev/null ||
    fail "duplicate, unsupported or out-of-range matrix dimension"

absolute_path() {
    local path=$1 link count=0
    while [ -L "$path" ]; do
        count=$((count + 1))
        [ "$count" -le 40 ] || fail "symlink loop: $1"
        link=$(readlink "$path")
        case "$link" in /*) path=$link ;; *) path=$(dirname "$path")/$link ;; esac
    done
    printf '%s/%s\n' "$(cd -P "$(dirname "$path")" && pwd)" "$(basename "$path")"
}
[ -f "$executable" ] || fail "executable does not exist: $executable"
[ -e "$certs" ] || fail "certificate path does not exist: $certs"
executable=$(absolute_path "$executable")
certs=$(absolute_path "$certs")
script=$(absolute_path "$0")
root=$(cd "$(dirname "$script")/.." && pwd -P)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/arknet-benchmark-matrix.XXXXXX")
atomic=
cleanup() {
    local code=$?
    if [ "$code" -eq 0 ]; then
        rm -rf "$scratch"
    else
        printf 'Benchmark diagnostics retained: %s\n' "$scratch" >&2
    fi
    if [ -n "$atomic" ]; then rm -f "$atomic"; fi
    exit "$code"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Path sorts by components: a directory "move" precedes the file "move.hpp".
# Match that order, then hash relative path NUL, contents NUL, excluding results.
source_hash() {
    (
        cd "$root"
        printf '%s\n' CMakeLists.txt vcpkg.json
        if [ -f vcpkg-configuration.json ]; then printf '%s\n' vcpkg-configuration.json; fi
        for directory in include cmake benchmarks ports; do
            [ -d "$directory" ] || continue
            find "$directory" \( -type f -o -type l \) ! -path '*/results/*' \
                \( -name '*.hpp' -o -name '*.h' -o -name '*.ipp' -o -name '*.cpp' \
                -o -name '*.py' -o -name '*.cmake' -o -name '*.in' -o -name '*.txt' -o -name '*.json' \)
        done
    ) | jq -Rrs 'split("\n") | map(select(length>0)) | unique | sort_by(split("/")) | .[]' \
        > "$scratch/source-paths"
    while IFS= read -r relative; do
        [ -f "$root/$relative" ] || continue
        printf '%s\0' "$relative"
        cat "$root/$relative" || return 1
        printf '\0'
    done < "$scratch/source-paths" | shasum -a 256 | awk '{print $1}'
}
source_digest=$(source_hash)
executable_digest=$(shasum -a 256 "$executable" | awk '{print $1}')
runner_digest=$(shasum -a 256 "$script" | awk '{print $1}')

system=$(uname -s)
processor=$(uname -p 2>/dev/null || true)
[ "$processor" != unknown ] || processor=
logical_cpus=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf null)
host=$(jq -cn --arg system "$system" --arg release "$(uname -r)" \
    --arg machine "$(uname -m)" --arg processor "$processor" --argjson cpus "$logical_cpus" \
    '{system:$system,release:$release,machine:$machine,processor:$processor,logical_cpus:$cpus}')
if [ "$system" = Darwin ]; then
    for mapping in model:hw.model cpu:machdep.cpu.brand_string physical_cpus:hw.physicalcpu memory_bytes:hw.memsize; do
        key=${mapping%%:*}
        if value=$(sysctl -n "${mapping#*:}" 2>/dev/null); then
            host=$(jq -cn --argjson host "$host" --arg key "$key" --arg value "$value" \
                '$host + {($key):(if $value | test("^[0-9]+$") then $value | tonumber else $value end)}')
        fi
    done
    for mapping in os_version:-productVersion os_build:-buildVersion; do
        if value=$(sw_vers "${mapping#*:}" 2>/dev/null); then
            host=$(jq -cn --argjson host "$host" --arg key "${mapping%%:*}" --arg value "$value" \
                '$host + {($key):$value}')
        fi
    done
fi
git_json() {
    local value
    if value=$(git -C "$root" "$@" 2>/dev/null); then
        jq -cn --arg value "$value" '$value | gsub("^\\s+|\\s+$";"")'
    else
        printf 'null\n'
    fi
}
mkdir -p "$(dirname "$output")"
output=$(absolute_path "$output")
atomic=$(mktemp "$output.tmp.XXXXXX")
jq -n --arg backend "$backend" --arg recorded "$(date -u '+%Y-%m-%dT%H:%M:%S+00:00')" \
    --argjson head "$(git_json rev-parse HEAD)" --argjson status "$(git_json status --porcelain)" \
    --arg source "$source_digest" --arg executable "$executable_digest" --arg runner "$runner_digest" \
    --argjson host "$host" --argjson settings "$settings" '
    {schema_version:2,backend:(if $backend == "" then null else $backend end),
     recorded_at_utc:$recorded,git_head:$head,git_status:$status,source_sha256:$source,
     executable_sha256:$executable,runner_sha256:$runner,host:$host,settings:$settings,
     results:[],complete:false,failed_measurements:0}' > "$atomic"
mv -f "$atomic" "$output"
jq -cn --argjson settings "$settings" --arg backend "$backend" '
    $settings as $s | range(1;$s.repetitions+1) as $r | $s.io_profiles[] as $io |
    $s.protocols[] as $p | $s.payloads[] as $bytes | $s.profiles[] as $profile |
    (($s.execution_modes // [null]) | if $r % 2 == 0 then reverse else . end)[] as $execution |
    {repetition:$r,protocol:$p,payload_bytes:$bytes,address_family:$s.address_family} + $profile + $io |
    if $p == "udp" and $s.udp_throughput_window == 1 then .window=1 else . end |
    if $backend != "" then .backend=$backend else . end |
    if $execution != null then .execution=$execution else . end' > "$scratch/cases"
limit=$(jq -cn --argjson seconds "$seconds" --argjson warmup "$warmup" '$seconds+$warmup+45')

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
        jq -e -s 'length==1 and (.[0]|type=="object") and (.[0]|[..|numbers]|all(isfinite))' "$1" >/dev/null
}
while IFS= read -r item; do
    jq -n --argjson item "$item" --arg executable "$executable" --arg certs "$certs" \
        --arg seconds "$seconds" --arg warmup "$warmup" '
        $item + {command:([$executable,"--protocol",$item.protocol,"--payload",($item.payload_bytes|tostring),
          "--clients",($item.clients|tostring),"--window",($item.window|tostring),"--seconds",$seconds,
          "--warmup",$warmup,"--certs",$certs,"--io-model",$item.io_model,
          "--io-threads",($item.io_threads|tostring),"--work",($item.handler_work|tostring),
          "--address-family",$item.address_family] +
          (if $item.execution then ["--execution",$item.execution] else [] end))}' > "$scratch/entry"
    command=()
    while IFS= read -r argument; do command+=("$argument"); done < <(jq -r '.command[]' "$scratch/entry")
    jq -r '"\(.repetition) \(.protocol) \(.payload_bytes) B clients=\(.clients) window=\(.window) \(.io_model):\(.io_threads) work=\(.handler_work) \(.execution // "arknet-callback")"' "$scratch/entry"
    returncode=0
    "$timeout_tool" --kill-after=5 "$limit" "${command[@]}" \
        > "$scratch/stdout" 2> "$scratch/stderr" || returncode=$?
    jq --argjson code "$returncode" --rawfile stderr "$scratch/stderr" \
        '. + {returncode:$code,stderr:$stderr}' "$scratch/entry" > "$scratch/updated"
    mv "$scratch/updated" "$scratch/entry"
    error=
    if strict_json "$scratch/stdout"; then
        jq -s '.[0]' "$scratch/stdout" > "$scratch/metrics"
        jq --slurpfile metrics "$scratch/metrics" '.metrics=$metrics[0]' "$scratch/entry" > "$scratch/updated"
        mv "$scratch/updated" "$scratch/entry"
        error=$(jq -r '
            . as $e | .metrics as $m |
            if $m.ok != true then "benchmark reported a failed measurement"
            elif any(["protocol","payload_bytes","clients","window","io_model","io_threads","handler_work","address_family"][];
                . as $key | $m[$key] != $e[$key]) then "benchmark returned different case parameters"
            elif ($m.messages|type) != "number" or ($m.elapsed_seconds|type) != "number" or
                $m.messages <= 0 or $m.elapsed_seconds <= 0 then "benchmark did not measure completed traffic"
            elif $e.execution and $m.implementation != ("asio-" + $e.execution) then
                "benchmark returned a different execution mode"
            elif $e.backend and $m.backend != $e.backend then "benchmark returned a different backend"
            else empty end' "$scratch/entry")
    else
        error="benchmark did not return valid finite JSON"
        jq --rawfile stdout "$scratch/stdout" '.stdout=$stdout' "$scratch/entry" > "$scratch/updated"
        mv "$scratch/updated" "$scratch/entry"
    fi
    if [ "$returncode" -eq 124 ]; then
        error="benchmark timed out after $limit seconds"
    elif [ "$returncode" -eq 137 ]; then
        error="benchmark received SIGKILL (timeout escalation or external termination)"
    elif [ "$returncode" -ne 0 ]; then
        error="benchmark failed with exit code $returncode"
    fi
    if [ -n "$error" ]; then
        jq --arg error "$error" '.error=$error' "$scratch/entry" > "$scratch/updated"
        mv "$scratch/updated" "$scratch/entry"
    fi
    jq --slurpfile entry "$scratch/entry" '.results += $entry |
        .failed_measurements += (if $entry[0].error then 1 else 0 end)' "$output" > "$atomic"
    mv -f "$atomic" "$output"
    if [ -n "$error" ]; then
        printf 'Failed: %s. Report: %s\n' "$error" "$output" >&2
        [ "$keep_going" -eq 1 ] || exit 1
    fi
done < "$scratch/cases"
jq '.complete=true' "$output" > "$atomic"
mv -f "$atomic" "$output"
jq -r '"Saved \(.results|length) measurements"' "$output"
jq -e '.failed_measurements == 0' "$output" >/dev/null || exit 1
