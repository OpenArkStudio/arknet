# Historical benchmark reports

The Markdown reports in this directory are retained as historical records. Their raw JSON, commands, source snapshots and logs are available at [the immutable archive commit](https://github.com/OpenArkStudio/arknet/tree/7685e241bebd086d50da403f2cf06a72bbef1085/benchmarks/results) and in the existing [v0.2.1 release attachments](https://github.com/OpenArkStudio/arknet/releases/tag/v0.2.1).

- `arknet-0.2.1-benchmark-comparison.tar.gz` contains `source/arknet-measured-2aa5bc0.tar`.
- `arknet-0.2.1-validation-logs.tar.gz` contains `historical/arknet-0.2.1-validation-logs/source/arknet-validation-6c8418e.tar`.
- Each nested source archive preserves the complete historical `benchmarks/results/` and `tests/results/` directories. Check the release's `.sha256` file and the archive's `SHA256SUMS` before use.

The archive commit identifies storage, not the source measured by every report. Keep each dataset's original source revision, executable checksum, commands, host details and measurement time when reproducing it.

With the archive commit available locally, extract only the evidence directories to a location outside the source checkout:

```sh
history_dir=/path/to/outside-source/arknet-history
mkdir -p "$history_dir"
git archive 7685e241bebd086d50da403f2cf06a72bbef1085 \
  benchmarks/results tests/results | tar -xf - -C "$history_dir"
```

Keep new raw JSON and logs outside the source repository; use release attachments or CI artifacts for shared evidence. Published [performance reports](../../docs/performance/index.md), SVG charts and compact numeric summaries remain under `docs/`. Deterministic inputs belong with their tests. The native C++ benchmark programs remain independently compilable; optional shell and offline report tools read the caller's external directory. Supply `--raw-base` or the RUDP report tools' `--raw-url` only after publishing an archive; otherwise input names remain unlinked.
