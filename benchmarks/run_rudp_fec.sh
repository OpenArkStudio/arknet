#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 ]]; then
    printf 'Usage: bash benchmarks/run_rudp_fec.sh BUILD_DIR OUTPUT_DIR\n' >&2
    printf 'Optional environment: RUDP_SECONDS, RUDP_WARMUP, RUDP_ROUNDS, RUDP_CLIENTS, RUDP_SEED,\n' >&2
    printf 'RUDP_PAYLOADS, RUDP_WINDOWS, RUDP_PROFILES, RUDP_POLICIES, RUDP_MODELS, RUDP_SOAK_SECONDS.\n' >&2
    exit 2
fi

benchmark="${1%/}/benchmarks/arknet_rudp_benchmark"
if [[ ! -x "$benchmark" && -x "$benchmark.exe" ]]; then
    benchmark="$benchmark.exe"
fi
if [[ ! -x "$benchmark" ]]; then
    printf 'Build arknet_rudp_benchmark first: %s\n' "$benchmark" >&2
    exit 2
fi
output_dir=$2
mkdir -p "$output_dir"
read -r -a payloads <<< "${RUDP_PAYLOADS:-64 1024 4096 16384}"
read -r -a windows <<< "${RUDP_WINDOWS:-1 16}"
read -r -a profiles <<< "${RUDP_PROFILES:-clean random burst}"
read -r -a policies <<< "${RUDP_POLICIES:-off fixed adaptive}"
read -r -a models <<< "${RUDP_MODELS:-shared:1 shared:4 sharded:4}"
seconds=${RUDP_SECONDS:-2}
warmup=${RUDP_WARMUP:-2}
rounds=${RUDP_ROUNDS:-3}
clients=${RUDP_CLIENTS:-4}
seed=${RUDP_SEED:-42}
failed=0

run() {
    local name=$1
    shift
    printf '%q ' "$benchmark" "$@" > "$output_dir/$name.command.sh"
    printf '\n' >> "$output_dir/$name.command.sh"
    printf 'Running %s\n' "$name" >&2
    if "$benchmark" "$@" > "$output_dir/$name.json" 2> "$output_dir/$name.stderr"; then
        printf '0\n' > "$output_dir/$name.exit"
    else
        local status=$?
        printf '%s\n' "$status" > "$output_dir/$name.exit"
        failed=1
    fi
}

for profile in "${profiles[@]}"; do
    case "$profile" in
        clean) impairment=(--profile clean --delay-ms 0) ;;
        random) impairment=(--profile random --loss-percent 5 --delay-ms 10 --duplicate-percent 1 --reorder-percent 5) ;;
        burst) impairment=(--profile burst --burst-count 3 --burst-period 100 --delay-ms 10 --duplicate-percent 1 --reorder-percent 5) ;;
        *) printf 'Unknown profile: %s\n' "$profile" >&2; exit 2 ;;
    esac
    for policy in "${policies[@]}"; do
        for model in "${models[@]}"; do
            case "$model" in
                shared:1|shared:4|sharded:4) ;;
                *) printf 'Unknown IO model: %s\n' "$model" >&2; exit 2 ;;
            esac
            for payload in "${payloads[@]}"; do
                for window in "${windows[@]}"; do
                    name="$profile-$policy-${model/:/}-p$payload-w$window"
                    run "$name" --payload "$payload" --clients "$clients" --window "$window" \
                        --seconds "$seconds" --warmup "$warmup" --rounds "$rounds" --seed "$seed" \
                        --io-model "${model%:*}" --io-threads "${model#*:}" --fec "$policy" "${impairment[@]}"
                done
            done
        done
    done
done

if [[ ${RUDP_SOAK_SECONDS:-0} != 0 ]]; then
    for policy in "${policies[@]}"; do
        run "soak-$policy-shared4" --payload 4096 --clients "$clients" --window 16 \
            --seconds "$RUDP_SOAK_SECONDS" --warmup "$warmup" --rounds 1 --seed "$seed" \
            --io-model shared --io-threads 4 --fec "$policy" --profile burst --burst-count 3 \
            --burst-period 100 --loss-percent 2 --delay-ms 10 --duplicate-percent 1 --reorder-percent 5
    done
fi
exit "$failed"
