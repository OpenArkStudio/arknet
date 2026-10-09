# Expanded Measurement Source Record

[中文](README_CN.md)

Source SHA-256: `fa69d6ce97745281290a263cb0f2515470ede0edb77f5132ce6e3ac7ae7cd9b4`.

This snapshot preserves the benchmark tools and build inputs used by the
2,376-measurement expanded matrix. Networking headers are verified against the
checkout using the recorded per-file hashes. Baseline data uses a separate
source record in `../127a500a/`.

The two native matrices use the archived `benchmark_matrix.sh` entry point.
Its `runner_sha256` is recorded separately; the measurement source SHA-256 is
unchanged.

```sh
python3 benchmarks/results/provenance/fa69d6ce/verify.py
```
