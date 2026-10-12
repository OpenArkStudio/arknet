#!/usr/bin/env bash
set -euo pipefail
cd /Users/nick/work/opensource/arknet
udp_probe_root=benchmarks/results/macmini-m4pro-20261012-udp/buffer-probes
udp_probe_executable=/private/tmp/arknet-macmini-standalone/benchmarks/arknet_loopback_benchmark
mkdir -p "$udp_probe_root"
shasum -a 256 "$udp_probe_executable" > "$udp_probe_root/executable-sha256.txt"
git diff -- benchmarks/loopback.cpp > "$udp_probe_root/benchmark-instrumentation.patch"
sysctl kern.ipc.maxsockbuf net.inet.udp.maxdgram net.inet.udp.recvspace > "$udp_probe_root/socket-limits.txt"
ifconfig lo0 > "$udp_probe_root/lo0.txt"
for repetition in 1 2 3; do
    for profile in 16384:64:1048576 16384:64:2097152 16384:64:4194304 16384:16:4194304 16384:1:4194304 1472:64:4194304 1024:1024:4194304; do
        IFS=: read -r payload window receive_buffer <<< "$profile"
        prefix="$udp_probe_root/r$repetition-p$payload-w$window-b$receive_buffer"
        netstat -s -p udp > "$prefix-before-udp.txt"
        netstat -s -p ip > "$prefix-before-ip.txt"
        status=0
        "$udp_probe_executable" --protocol udp --payload "$payload" --clients 16 --window "$window" --io-model shared --io-threads 1 --seconds 1 --warmup 0 --udp-server-receive-buffer "$receive_buffer" > "$prefix.json" 2> "$prefix.stderr" || status=$?
        printf '%s\n' "$status" > "$prefix.exit"
        netstat -s -p udp > "$prefix-after-udp.txt"
        netstat -s -p ip > "$prefix-after-ip.txt"
        jq -c '{payload_bytes,window,udp_server_receive_buffer_bytes,messages_sent,server_received_messages,messages,udp_lost_messages,order_errors,content_errors}' "$prefix.json"
    done
done
