# Performance Methodology and Metrics

Measure locally with independent Release C++ programs. CI runs regressions and smoke checks; published reports record the host, configuration, raw results and scope.

Keep raw JSON and logs outside the source repository. Replace `/path/to/arknet-results` below with an existing external directory. Offline report tools accept `--raw-base` (`--raw-url` for RUDP) for a published archive; without it, input names appear without raw-data links.

## Single Benchmark

```sh
build/benchmarks/arknet_loopback_benchmark \
  --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1
build/benchmarks/arknet_coroutine_benchmark \
  --execution coroutine --protocol tcp --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --work 0 --warmup 0.25 --seconds 1
```

Each executable writes one JSON result to standard output and returns nonzero
on failure. Repeat a configuration in fresh processes with a short shell loop:

```sh
for repetition in 1 2 3; do
  build/benchmarks/arknet_loopback_benchmark \
    --protocol tcp --payload 1024 --clients 16 --window 16 \
    --io-model shared --io-threads 1 --work 0 --warmup 0.25 --seconds 1 \
    > "/path/to/arknet-results/tcp-${repetition}.json" || exit 1
done
```

## Performance Matrix

The original IO-topology baseline and the expanded workload matrix are separate
datasets. [Payload, batch and execution comparisons](dimensions.md)
hold connection counts constant while varying the other dimensions.

### Payload, Batch and Execution Modes

Maintainers can collect complete matrices with `bash scripts/benchmark_matrix.sh`.
Only this batch tool requires `jq`, GNU `timeout` (`gtimeout` on macOS) and
`shasum`, in addition to the usual shell tools. It launches the same C++ programs
and preserves their commands, JSON, failures, host details and hashes.

```sh
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_loopback_benchmark \
  --backend standalone --certs tests/certs --clients 16 --windows 1 4 16 64 \
  --payloads 64 1024 16384 --io-models shared sharded --io-threads 1 4 \
  --seconds 1 --warmup 0.25 --repetitions 3 --keep-going \
  --output /path/to/arknet-results/local-dimensions-standalone-callback.json
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_coroutine_benchmark \
  --backend standalone --certs tests/certs --protocols tcp --clients 16 \
  --windows 1 4 16 64 --payloads 64 1024 16384 \
  --io-models shared sharded --io-threads 1 4 --work-values 0 10000 \
  --execution-modes callback coroutine --seconds 1 --warmup 0.25 --repetitions 3 \
  --output /path/to/arknet-results/local-dimensions-standalone-native.json
```

Repeat both commands with the executables from the Boost build directory, set
`--backend boost`, and write `local-dimensions-boost-callback.json` and
`local-dimensions-boost-native.json`. These matrices contain **756 arknet
callback measurements and 432 matched native TCP measurements per dependency configuration**.
All seven protocols use the four explicit outstanding-request limits, including
UDP. Batch size and connection count are independent settings.

The native program compares Asio callbacks and C++20 coroutines with identical
full-batch server reads/echoes, full-batch client writes/reads, payload checks, CPU
computation and per-connection strands. It measures batch completion RTT;
one request per batch is equivalent to message RTT. Public arknet coroutine
endpoint APIs remain TODO. Its CPU time includes warmup and measurement for
both execution modes. Each server batch applies the configured computation to
every payload-sized message before echoing the batch.

`--keep-going` retains unsuccessful load cases, finishes the requested matrix
and returns nonzero if any case failed. Failed groups remain visible with their
errors and UDP losses; they are excluded from valid throughput/RTT comparisons.
Each configuration runs three times in a fresh process. Native execution order
alternates between repetitions. This closed-loop load reports response RTT,
including queueing; it does not measure an independently scheduled arrival rate.
Optionally create the offline report environment under
[Metrics and Charts](#metrics-and-charts), then generate the comparison from all
four data files after measurement has finished:

```sh
build/report-venv/bin/python benchmarks/dimensions.py \
  --reports /path/to/arknet-results/local-dimensions-standalone-callback.json \
            /path/to/arknet-results/local-dimensions-boost-callback.json \
            /path/to/arknet-results/local-dimensions-standalone-native.json \
            /path/to/arknet-results/local-dimensions-boost-native.json \
  --output-dir docs/performance
```

### Original IO-Topology Baseline

```sh
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_loopback_benchmark \
  --certs tests/certs --seconds 1 --warmup 0.25 --repetitions 3 \
  --output /path/to/arknet-results/local-loopback.json
bash scripts/benchmark_matrix.sh --executable build/benchmarks/arknet_coroutine_benchmark \
  --certs tests/certs --protocols tcp --work-values 0 10000 \
  --seconds 1 --warmup 0.25 --repetitions 3 \
  --output /path/to/arknet-results/local-coroutine.json
```

Per dependency configuration, the callback matrix has **630 measurements**:
seven protocols × three payloads × two traffic profiles × five topologies ×
three repetitions. The coroutine TCP matrix has **180 measurements**, adding
two handler-work levels. Default runner durations are three measured seconds
and one warmup second; the commands above match the shorter local baseline.
No-TLS runs should specify `--protocols tcp udp websocket http`.
These commands reproduce the baseline load settings on the current checkout.
The published baseline records its original source and binary hashes; use that
archived source to reproduce the earlier implementation.

| Dimension | Values |
| --- | --- |
| Protocol | TCP with `use_dgram`, UDP, WebSocket, TCPS, WSS, HTTP, HTTPS |
| Payload | 64, 1024, 16384 bytes |
| Traffic | 1 client / window 1; 16 clients / window 16 (UDP window 1) |
| IO | One context run by 1, 2 or 4 threads; 2 or 4 contexts run by one thread each |
| Coroutine work | 0 or 10000 deterministic CPU iterations per server echo |

One `io_context` can run on one or several threads. Multithreaded configurations
use independent per-connection strands; partitioned configurations run each
context on one thread. Both endpoints share the IO scheduling resources.
With one context the two configurations are identical, so the matrix measures
that case once. [IO models and coroutine guidance](../concepts/io-models.md) explains the
business tradeoffs.

Every measurement starts in a fresh process, warms up, measures, then drains
pending traffic for at most two seconds. Stop concurrent builds and other
heavy processes before collecting data. The runner saves partial failures;
content/order errors, rejected/completion-error sends, lost UDP messages,
unexpected disconnects and unfinished traffic invalidate a measurement.

## Metrics and Charts

| Metric | Definition |
| --- | --- |
| Round trips/s | Completed echoes divided by measured wall time |
| Payload MiB/s | `2 × payload bytes × completed echoes / seconds / 1048576`; headers and encryption excluded |
| RTT p50/p95/p99 | From submission to matching echo, including queueing and handler work; sampled |
| Average CPU utilization | Process CPU time divided by wall time over the same interval, multiplied by 100%; one core is 100%, multithreaded usage can exceed 100% |
| Peak RSS | Whole-process peak resident bytes; not a leak proof |
| Errors | Content/order, send completion/admission, lost/unfinished traffic, exceptions and drain timeouts |

CPU includes all client and server threads in the same process, not power consumption.
On this 14-logical-core host, process usage of 400% is about 28.6% of total CPU
capacity. New executables record `cpu_elapsed_seconds`. Historical results did not
record a separate wall-clock CPU sampling interval, so reports label utilization
as estimated: measurement plus drain for protocol tests, warmup plus measurement
for the matched native callback/coroutine comparison.

Both benchmark executables and the batch script accept `--address-family ipv6`
to test `::1`; the default `ipv4` uses `127.0.0.1`. Published performance reports
measure IPv4. See [IPv4 and IPv6](../guides/ipv4-ipv6.md) for usage and validation scope.

UDP requests 64 KiB send and 4 MiB receive buffers. Raw JSON records actual
kernel sizes for the server and client min/max; the OS can clamp requests.
The default UDP window does not model an unreliable network.
The measured host's loopback interface `lo0` has an MTU of 16384 bytes.

Python and Matplotlib are optional offline report tools. They are not used to
compile, launch or measure the C++ network tests. After measurement, generate
charts and bilingual tables from complete runs on the same host, source hash
and measurement duration:

```sh
python3 -m venv build/report-venv
build/report-venv/bin/pip install -r benchmarks/requirements-report.txt
build/report-venv/bin/python benchmarks/report.py \
  --reports /path/to/arknet-results/local-loopback.json /path/to/arknet-results/local-coroutine.json \
  --output-dir docs/performance \
  --host-description "Actual hardware, compiler, dependencies and Release build flags"
```

Windows uses `build/report-venv/Scripts/python.exe`. Matplotlib is only a
report-generation dependency. The generator refuses incomplete, failed,
duplicate or incompatible measurements. Charts show medians and min/max
across repetitions; these whiskers are not confidence intervals. Reported
latency is a median of per-run percentiles, not a pooled percentile.

Report-tool maintainers can run the optional Python checks separately:

```sh
build/report-venv/bin/python -m unittest discover -s benchmarks -p 'test_report.py' -v
build/report-venv/bin/python -m unittest discover -s benchmarks -p 'test_dimensions.py' -v
```

With several requests in flight per connection, the coroutine benchmark measures
a **batch RTT**, whereas callback traffic
replenishes a window and measures **message RTT**. Compare topology within
a benchmark program; do not infer a coroutine-versus-callback speedup from this
original baseline. The matched native comparison above uses one measurement
method for both execution modes.
