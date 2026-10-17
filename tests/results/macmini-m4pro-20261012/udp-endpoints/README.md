# UDP Client Endpoint Regression

## Failure Evidence

[CI run 38049776409](https://github.com/OpenArkStudio/arknet/actions/runs/38049776409)
failed only `benchmark_udp_sharded4`: 16 clients, 1024-byte payloads, one
outstanding request per client, four contexts with one thread each. Measurement
reported one content error, one missing reply and a drain timeout; warmup also
failed. That log did not record local ports.

A [Linux reproduction](https://github.com/OpenArkStudio/arknet/actions/runs/38193718991)
used the original implementation with endpoint/content diagnostics and 256 clients
to increase collision probability. It recorded:

| Shared local endpoint | Sending client | Client receiving its reply |
| --- | ---: | ---: |
| `127.0.0.1:56562` | 65 | 127 |
| `127.0.0.1:49583` | 73 | 247 |

Both pairs had `SO_REUSEADDR=1`; wrong-client replies appeared in warmup and
measurement. Measurement reported two content errors, two missing replies and
one drain timeout. This reproduces the original failure pattern. The original
16-client run cannot be retrospectively checked for duplicate ports.

The diagnostic wrapper also failed because `rg` was unavailable after it printed
the evidence. Its run is reproduction evidence, not a passing test result.
The raw output is retained in [linux-before.log](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/udp-endpoints/linux-before.log).

After changing only the default reuse setting, the same diagnostic program passed
**50 consecutive 256-client runs** on Linux, with no duplicate endpoints,
content errors or missing replies:
[run 38194451308](https://github.com/OpenArkStudio/arknet/actions/runs/38194451308),
[raw results](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/udp-endpoints/linux-after.log).

## Change and Coverage

UDP clients no longer implicitly enable `SO_REUSEADDR`. Explicit configuration
through `bind_init` remains available. The benchmark rejects duplicate client
endpoints before traffic starts; content, completion and loss checks remain strict.

The C++ regression in `tests/udp.cpp` checks IPv4 and IPv6 default options,
conflicts for the same local/remote endpoint pair, and explicit reuse opt-in.
TCP client defaults retain their existing behavior.

## Local Results

2026-10-12, Mac mini `Mac16,11`, Apple M4 Pro, 14 cores, 48 GiB RAM,
macOS 27.0.1 (26A434), Apple Clang 21.0.0, CMake 4.3.3, Asio 1.38.2,
Boost 1.90.0 and OpenSSL 3.6.4. Source base: `8526ad840eef5a557e01267ccd5f6582602d8023`
plus the UDP endpoint fix in the commit containing this record.

| Check | Result | Evidence |
| --- | --- | --- |
| Standalone + TLS, Release, complete checks | 41 checks / 134 cases passed | [Log](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/udp-endpoints/standalone-check.log) |
| Boost + TLS, Release, complete checks | 41 checks / 134 cases passed | [Log](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/udp-endpoints/boost-check.log) |
| Standalone UDP, ASan/UBSan | 14 cases passed | [Log](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/udp-endpoints/udp-asan.log) |
| Standalone UDP, TSan | 14 cases passed | [Log](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/udp-endpoints/udp-tsan.log) |
| Original failing configuration, Standalone | 30 fresh-process runs passed | [JSONL](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/udp-endpoints/standalone-repeated.jsonl) |
| Original failing configuration, Boost | 30 fresh-process runs passed | [JSONL](https://github.com/OpenArkStudio/arknet/blob/7685e241bebd086d50da403f2cf06a72bbef1085/tests/results/macmini-m4pro-20261012/udp-endpoints/boost-repeated.jsonl) |

All 60 repeated runs had successful warmup, zero content errors, zero missing
replies and zero drain timeouts. These short checks establish regression stability;
their timings are not published performance measurements.

## Run the Regression

```sh
build/tests/arknet_udp_test --test-case="UDP clients reserve exclusive*"
build/benchmarks/arknet_loopback_benchmark --protocol udp --payload 1024 \
  --clients 16 --window 1 --seconds 0.2 --warmup 0.1 \
  --io-model sharded --io-threads 4
```
