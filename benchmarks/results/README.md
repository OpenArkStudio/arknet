# Published benchmark data

This directory contains the raw data used by the [performance reports](../../docs/performance/index.md). JSON records are kept with machine details, workload parameters and source checksums so readers can verify the charts and regenerate them.

- Commit a dataset only when it supports a published report.
- Replace a report's previous dataset when publishing replacement results; Git history preserves earlier versions.
- Write development runs and diagnostics to an ignored build directory or `benchmarks/local-results/`.
- Store CI test logs in workflow artifacts. Avoid committing repeated execution logs and failed diagnostic runs.
- Keep compact, deterministic test inputs with the relevant tests; they are not benchmark output.

The C++ benchmark executables run independently. Optional shell and report tools operate on the output directory selected by the user.
