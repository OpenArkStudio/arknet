#!/usr/bin/env bash
set -euo pipefail
# Run from the repository root; preserve kernel counters around the exact matrix.
udp_result_dir=benchmarks/results/macmini-m4pro-20261010-udp-limits
snapshot() {
    local phase=$1
    date -u '+%Y-%m-%dT%H:%M:%SZ' > "$udp_result_dir/standalone-$phase-timestamp.txt"
    netstat -s -p udp > "$udp_result_dir/standalone-$phase-netstat-udp.txt" 2>&1
    netstat -s -p ip > "$udp_result_dir/standalone-$phase-netstat-ip.txt" 2>&1
    ifconfig lo0 > "$udp_result_dir/standalone-$phase-lo0.txt" 2>&1
    sysctl net.inet.udp.maxdgram net.inet.udp.recvspace > "$udp_result_dir/standalone-$phase-udp-sysctl.txt" 2>&1
}
snapshot before
matrix_exit=0
bash scripts/benchmark_matrix.sh --executable /private/tmp/arknet-macmini-standalone/benchmarks/arknet_loopback_benchmark --backend standalone --certs tests/certs --protocols udp --payloads 1472 8192 16356 16357 16384 65507 --clients 16 --windows 1 16 64 --io-models shared --io-threads 1 --work-values 0 --seconds 1 --warmup 0.25 --repetitions 3 --keep-going --output "$udp_result_dir/standalone.json" > "$udp_result_dir/standalone.log" 2>&1 || matrix_exit=$?
snapshot after
exit "$matrix_exit"
