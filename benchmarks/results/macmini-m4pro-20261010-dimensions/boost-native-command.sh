#!/usr/bin/env bash
set -euo pipefail
# Run from the repository root; the measured executable is the frozen Release build.
bash scripts/benchmark_matrix.sh --executable /private/tmp/arknet-macmini-boost/benchmarks/arknet_coroutine_benchmark --backend boost --certs tests/certs --clients 16 --windows 1 4 16 64 --payloads 64 1024 16384 --io-models shared sharded --io-threads 1 4 --seconds 1 --warmup 0.25 --repetitions 3 --protocols tcp --work-values 0 10000 --execution-modes callback coroutine --keep-going --output benchmarks/results/macmini-m4pro-20261010-dimensions/boost-native.json > benchmarks/results/macmini-m4pro-20261010-dimensions/boost-native.log 2>&1
