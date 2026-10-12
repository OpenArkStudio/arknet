#!/usr/bin/env bash
# Validate the collection tool using a fake executable; no sockets or compiler are used.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd -P)
runner="$root/scripts/rudp_matrix.sh"
command -v jq >/dev/null || { printf 'jq is required\n' >&2; exit 2; }
scratch=$(mktemp -d "${TMPDIR:-/tmp}/arknet-rudp-selfcheck.XXXXXX")
cleanup() {
    local code=$? log
    if [ "$code" -eq 0 ]; then
        rm -rf "$scratch"
    else
        for log in "$scratch"/*.log; do
            [ -f "$log" ] || continue
            printf '\n%s\n' "$(basename "$log")" >&2
            cat "$log" >&2
        done
        printf 'Self-check diagnostics retained: %s\n' "$scratch" >&2
    fi
    exit "$code"
}
trap cleanup EXIT
build="$scratch/build with spaces"
include="$scratch/include with spaces"
mkdir -p "$build/benchmarks" "$include/asio"
printf '#define ASIO_VERSION 103802\n' > "$include/asio/version.hpp"
cat > "$scratch/fake-cxx" <<'COMPILER'
#!/usr/bin/env bash
printf 'Fake compiler version 1.0 for runner tests\n'
COMPILER
chmod +x "$scratch/fake-cxx"
printf '%s\n' "CMAKE_CXX_COMPILER:FILEPATH=$scratch/fake-cxx" 'CMAKE_BUILD_TYPE:STRING=Release' \
    'CMAKE_CXX_FLAGS:STRING=' 'CMAKE_CXX_FLAGS_RELEASE:STRING=-O3 -DNDEBUG' \
    'ARKNET_USE_BOOST_ASIO:BOOL=OFF' "ARKNET_ASIO_INCLUDE_DIR:PATH=$include" > "$build/CMakeCache.txt"
cat > "$build/benchmarks/arknet_rudp_benchmark" <<'BENCHMARK'
#!/usr/bin/env bash
set -euo pipefail
while [ "$#" -gt 0 ]; do
    key=$1 value=$2
    shift 2
    case "$key" in
        --payload) payload=$value ;; --clients) clients=$value ;; --window) window=$value ;;
        --io-model) model=$value ;; --io-threads) threads=$value ;;
        --seconds) seconds=$value ;; --warmup) warmup=$value ;; --rounds) rounds=$value ;;
        *) exit 9 ;;
    esac
done
case "${RUDP_FAKE_MODE:-ok}" in
    invalid) printf '{invalid\n'; exit 0 ;;
    nan) printf '{"ok":true,"cpu_percent":NaN}\n'; exit 0 ;;
    parameters) payload=$((payload + 1)) ;;
esac
jq -cn --arg model "$model" --argjson payload "$payload" --argjson clients "$clients" \
    --argjson window "$window" --argjson threads "$threads" --argjson seconds "$seconds" \
    --argjson warmup "$warmup" --argjson rounds "$rounds" '
    {ok:true,protocol:"rudp",wire_protocol:"kcp",api:"callback",backend:"standalone",
      payload_bytes:$payload,clients:$clients,window:$window,io_model:$model,io_threads:$threads,
      round_count:$rounds,requested_seconds:$seconds,warmup_seconds:$warmup,
      udp_receive_buffer_requested_bytes:4194304,
      summary:{successful_rounds:$rounds,failures:0},
      rounds:[range(1;$rounds+1) | {round:.,ok:true,messages:10,messages_sent:10,
        udp_server_receive_buffer_bytes:65536,udp_client_receive_buffer_min_bytes:65536,
        udp_client_receive_buffer_max_bytes:131072,
        server_received_messages:10,pending_completions:0,elapsed_seconds:1,failures:0}]} '
if [ "${RUDP_FAKE_MODE:-ok}" = failure ]; then
    printf 'simulated failure\n' >&2
    exit 7
fi
BENCHMARK
chmod +x "$build/benchmarks/arknet_rudp_benchmark"
common=(--build-dir "$build" --backend standalone --backend-include "$include"
    --seconds 0.001 --warmup 0 --rounds 1 --clients 1 --payloads 64 --windows 1 --profiles shared:1)
run() {
    local name=$1
    shift
    bash "$runner" "${common[@]}" --output "$scratch/$name" "$@" > "$scratch/$name.log" 2>&1
}
expect_failure() {
    local name=$1
    shift
    if run "$name" "$@"; then
        printf 'unexpected success: %s\n' "$name" >&2
        exit 1
    fi
}
run valid
jq -e '.complete and .backend_version=="1.38.2" and (.cases|length)==1 and
    .compiler_version=="Fake compiler version 1.0 for runner tests" and
    .compiler_flags==" -O3 -DNDEBUG" and
    all([.source_sha256,.binary_sha256,.runner_sha256][]; test("^[0-9a-f]{64}$")) and
    (.started_at_utc|length)>0 and (.finished_at_utc|length)>0 and .host.logical_cpus>0' \
    "$scratch/valid/metadata-standalone.json" >/dev/null
jq -e '.ok and .provenance.returncode==0 and (.provenance.command|index("--payload"))!=null and
    (.provenance.started_at_utc|length)>0 and (.provenance.finished_at_utc|length)>0' \
    "$scratch/valid/standalone-payload64-window1-shared1.json" >/dev/null
metadata_digest=$(shasum -a 256 "$scratch/valid/metadata-standalone.json")
result_digest=$(shasum -a 256 "$scratch/valid/standalone-payload64-window1-shared1.json")
expect_failure valid
[ "$(shasum -a 256 "$scratch/valid/metadata-standalone.json")" = "$metadata_digest" ]
[ "$(shasum -a 256 "$scratch/valid/standalone-payload64-window1-shared1.json")" = "$result_digest" ]
for mode in invalid nan failure parameters; do
    export RUDP_FAKE_MODE=$mode
    expect_failure "$mode"
    jq -e '.complete==false and (.cases|length)==1' "$scratch/$mode/metadata-standalone.json" >/dev/null
done
unset RUDP_FAKE_MODE
jq -e '.ok==false and .raw_stdout=="{invalid\n" and .provenance.returncode==0' \
    "$scratch/invalid/standalone-payload64-window1-shared1.json" >/dev/null
jq -e '.ok==false and (.raw_stdout|contains("NaN"))' \
    "$scratch/nan/standalone-payload64-window1-shared1.json" >/dev/null
jq -e '.ok and .provenance.returncode==7 and .provenance.stderr=="simulated failure\n"' \
    "$scratch/failure/standalone-payload64-window1-shared1.json" >/dev/null
expect_failure backend --backend boost
expect_failure missing --clients
expect_failure duration --seconds NaN
expect_failure fractional_rounds --rounds 1.5
expect_failure payload --payloads 16385
expect_failure window --windows 0
expect_failure duplicate --payloads 64 64
expect_failure profile --profiles shared:0
expect_failure bounds --clients 1024 --windows 1024 --payloads 16384
for name in backend missing duration fractional_rounds payload window duplicate profile bounds; do
    [ ! -e "$scratch/$name/metadata-standalone.json" ]
done
printf 'rudp_matrix shell self-check: PASS\n'
