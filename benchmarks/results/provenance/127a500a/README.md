# Measurement Source Record

[中文](README_CN.md)

The 1,620-measurement `macmini-m4pro-20261010-*.json` baseline records source
SHA-256 `127a500ad7e2fbe4c831cc3ebacc63acac96013e2cb8bf3893d7997ca6ea7f56`.

`manifest.json` lists every measured input and its SHA-256. This directory
preserves the original runner, reporting scripts and coroutine program that
were subsequently changed for readable SVG reports and expanded workload tests.
The raw measurements retain their original metadata. The expanded matrix is a
separate dataset in `benchmarks/results/macmini-m4pro-20261010-dimensions/`.

Verify the measured sources from the repository root:

```sh
python3 benchmarks/results/provenance/127a500a/verify.py
```

Verification uses the manifest's original file list and archived overrides,
so newly added benchmark tools do not change the reconstructed baseline hash.
Unchanged networking headers and build inputs are read from the checkout.
Identical archived inputs may be reused from the adjacent `fa69d6ce` record.
Older diagnostic runs are retained under `benchmarks/results/diagnostics/`.
