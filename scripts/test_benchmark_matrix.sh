#!/usr/bin/env bash
# Maintainer self-check with a fake executable; does not build or use sockets.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd -P)
runner=$root/scripts/benchmark_matrix.sh
command -v jq >/dev/null || { printf 'jq is required\n' >&2; exit 2; }
if command -v timeout >/dev/null 2>&1; then
    real_timeout=$(command -v timeout)
elif command -v gtimeout >/dev/null 2>&1; then
    real_timeout=$(command -v gtimeout)
else
    printf 'GNU timeout or gtimeout is required\n' >&2
    exit 2
fi
scratch=$(mktemp -d "${TMPDIR:-/tmp}/arknet-matrix-selfcheck.XXXXXX")
cleanup() {
    local code=$? log
    if [ "$code" -ne 0 ]; then
        for log in "$scratch"/*.log; do
            [ -f "$log" ] || continue
            printf '\n%s\n' "$(basename "$log")" >&2
            cat "$log" >&2
        done
        printf 'Self-check diagnostics retained: %s\n' "$scratch" >&2
    else
        rm -rf "$scratch"
    fi
    exit "$code"
}
trap cleanup EXIT
cat > "$scratch/fake" <<'FAKE'
#!/usr/bin/env bash
set -euo pipefail
protocol=tcp payload=64 clients=1 window=1 model=shared threads=1 work=0 execution= family=ipv4
while [ "$#" -gt 0 ]; do
    key=$1 value=$2
    shift 2
    case "$key" in
        --protocol) protocol=$value ;; --payload) payload=$value ;; --clients) clients=$value ;;
        --window) window=$value ;; --io-model) model=$value ;; --io-threads) threads=$value ;;
        --work) work=$value ;; --execution) execution=$value ;;
        --address-family) family=$value ;;
        --certs|--seconds|--warmup) ;;
        *) exit 9 ;;
    esac
done
mode=${FAKE_MODE:-ok}
case "$mode" in
    timeout) printf 'waiting\n'; printf 'slow\n' >&2; sleep 2 ;;
    invalid) printf '{invalid\n'; exit 0 ;;
    killed) exit 137 ;;
esac
backend=standalone
[ "$mode" != backend ] || backend=boost
implementation=arknet-callback
[ -z "$execution" ] || implementation=asio-$execution
[ "$mode" != execution ] || implementation=asio-coroutine
[ "$mode" != parameters ] || payload=$((payload + 1))
jq -cn --arg protocol "$protocol" --arg backend "$backend" --arg implementation "$implementation" \
    --arg family "$family" \
    --arg model "$model" --argjson payload "$payload" --argjson clients "$clients" \
    --argjson window "$window" --argjson threads "$threads" --argjson work "$work" '
    {ok:true,backend:$backend,implementation:$implementation,protocol:$protocol,address_family:$family,
     payload_bytes:$payload,clients:$clients,window:$window,io_model:$model,io_threads:$threads,
     handler_work:$work,messages:10,elapsed_seconds:1,roundtrips_per_second:10,
     payload_mib_per_second:1,rtt_p50_us:1,rtt_p95_us:2,rtt_p99_us:3,peak_rss_bytes:1024}'
if [ "$mode" = failure ] && [ "$window" -eq 4 ]; then printf 'failed load\n' >&2; exit 7; fi
FAKE
cat > "$scratch/timeout" <<'TIMEOUT'
#!/usr/bin/env bash
set -euo pipefail
if [ "${FAKE_MODE:-ok}" = timeout ]; then
    shift 2
    exec "$FAKE_REAL_TIMEOUT" --kill-after=1 0.1 "$@"
fi
exec "$FAKE_REAL_TIMEOUT" "$@"
TIMEOUT
chmod +x "$scratch/fake" "$scratch/timeout"
export FAKE_REAL_TIMEOUT=$real_timeout
export PATH=$scratch:$PATH
common=(--executable "$scratch/fake" --certs "$scratch" --backend standalone
    --protocols tcp --payloads 64 --clients 1 --io-models shared --io-threads 1
    --seconds 0.001 --warmup 0)
run() {
    local name=$1
    shift
    "$runner" "${common[@]}" --output "$scratch/$name.json" "$@" \
        > "$scratch/$name.log" 2>&1
}
expect_failure() {
    local name=$1
    shift
    if run "$name" "$@"; then printf 'unexpected success: %s\n' "$name" >&2; exit 1; fi
}
check() { jq -e "$2" "$scratch/$1.json" >/dev/null; }
run defaults
check defaults '.schema_version==2 and .complete and .failed_measurements==0 and (.results|length)==3 and
    .settings.repetitions==3 and (.source_sha256|test("^[0-9a-f]{64}$")) and
    (.executable_sha256|test("^[0-9a-f]{64}$")) and (.runner_sha256|test("^[0-9a-f]{64}$")) and
    .backend=="standalone" and all(.results[]; .returncode==0 and .backend=="standalone" and
        .metrics.ok and (.command|index("--certs"))!=null)'
expected_runner=$(shasum -a 256 "$runner" | awk '{print $1}')
jq -e --arg expected "$expected_runner" '.runner_sha256==$expected' "$scratch/defaults.json" >/dev/null
if [ "$(uname -s)" = Darwin ]; then
    jq -e --arg processor "$(uname -p)" '.host as $host |
        $host.system=="Darwin" and $host.processor==$processor and
        all(["release","machine","logical_cpus","model","cpu","physical_cpus","memory_bytes","os_version","os_build"][];
            . as $key | $host | has($key))' "$scratch/defaults.json" >/dev/null
fi
run dimensions --clients 1 2 --windows 1 4 --payloads 64 128 --execution-modes callback coroutine
check dimensions '(.results|length)==48 and .settings.execution_modes==["callback","coroutine"] and
    ([.results[]|[.clients,.window]]|unique|length)==4 and
    [.results[0].execution,.results[1].execution,.results[16].execution,.results[17].execution,
     .results[32].execution,.results[33].execution]==["callback","coroutine","coroutine","callback","callback","coroutine"]'
run udp_explicit --protocols udp --clients 1 2 --windows 1 4 --repetitions 1
check udp_explicit '.settings.udp_throughput_window==null and (.results|length)==4 and
    ([.results[].window]|unique)==[1,4]'
run ipv6 --address-family ipv6 --repetitions 1
check ipv6 '.settings.address_family=="ipv6" and .results[0].address_family=="ipv6" and
    .results[0].metrics.address_family=="ipv6" and (.results[0].command|index("--address-family"))!=null'
run udp_default --protocols udp --repetitions 1
check udp_default '.settings.udp_throughput_window==1 and all(.results[]; .window==1)'
export FAKE_MODE=failure
expect_failure keep_going --windows 1 4 --keep-going
check keep_going '.complete and .failed_measurements==3 and (.results|length)==6 and
    all(.results[]|select(.window==4); .returncode==7 and .stderr=="failed load\n" and .metrics.ok and (.error|length)>0)'
expect_failure stop --windows 1 4
check stop '(.complete|not) and .failed_measurements==1 and (.results|length)==2'
for mode in backend execution parameters invalid; do
    export FAKE_MODE=$mode
    expect_failure "$mode" --repetitions 1 --execution-modes callback
    check "$mode" '.failed_measurements==1 and (.results[0].error|length)>0'
done
check backend '.results[0].error=="benchmark returned a different backend"'
check execution '.results[0].error=="benchmark returned a different execution mode"'
check parameters '.results[0].error=="benchmark returned different case parameters"'
check invalid '.results[0].stdout=="{invalid\n" and .results[0].returncode==0'
export FAKE_MODE=timeout
expect_failure timeout --windows 1 4 --repetitions 1 --keep-going
check timeout '.complete and .failed_measurements==2 and (.results|length)==2 and
    all(.results[]; .returncode==124 and .stdout=="waiting\n" and .stderr=="slow\n" and
        (.error|contains("timed out")))'
export FAKE_MODE=killed
expect_failure killed --repetitions 1
check killed '.results[0].returncode==137 and (.results[0].error|contains("SIGKILL")) and
    (.results[0].error|contains("timed out")|not)'
retained=$(sed -n 's/^Benchmark diagnostics retained: //p' "$scratch/killed.log")
[ -f "$retained/entry" ] && [ -f "$retained/stdout" ] && [ -f "$retained/stderr" ]
unset FAKE_MODE
for dimension in protocols io-models io-threads work-values payloads clients windows execution-modes; do
    case "$dimension" in
        protocols) value=tcp ;; io-models) value=shared ;; execution-modes) value=callback ;;
        payloads) value=64 ;; work-values) value=0 ;; *) value=1 ;;
    esac
    expect_failure "duplicate-$dimension" "--$dimension" "$value" "$value"
    [ ! -e "$scratch/duplicate-$dimension.json" ]
done
expect_failure bounds --clients 1024 --windows 1024 --payloads 65507
expect_failure native_bounds --clients 65 --execution-modes callback
expect_failure duration --seconds NaN
expect_failure threads --io-threads 0
expect_failure payload --payloads 15
expect_failure repetitions --repetitions 0
expect_failure family --address-family ipv7
[ ! -e "$scratch/bounds.json" ] && [ ! -e "$scratch/native_bounds.json" ]
printf 'benchmark_matrix shell self-check: PASS\n'
