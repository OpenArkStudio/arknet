# Native Asio TCP: Callbacks and Coroutines Load Comparison

16 clients; 64/1024/16384 bytes; windows 1/4/16/64; three runs/configuration.

Native Asio callbacks/coroutines share fixed-size TCP batches: the server reads a full batch, applies the same computation per message, then writes the entire reply. Window 1 records message RTT; larger windows batch RTT. Public arknet coroutine endpoints remain TODO.

<Aside type="note" title="CPU">

Process CPU: 100% per logical CPU; multiple threads can exceed 100%. Estimates are labeled; incomplete three-sample sets show N/A. On this 14-logical-CPU host, machine share = process CPU% / 14; 100% of one CPU is 7.14% of the machine.

</Aside>

## No extra application computation

![Round-trip throughput](assets/dimensions-native-tcp-iterations0-throughput.svg)

![Bidirectional application payload throughput](assets/dimensions-native-tcp-iterations0-payload.svg)

![Batch p99 RTT](assets/dimensions-native-tcp-iterations0-latency.svg)

![Average process CPU utilization](assets/dimensions-native-tcp-iterations0-cpu.svg)

![Coroutine/callback throughput ratio](assets/dimensions-native-tcp-iterations0-ratio.svg)

![Coroutine/callback CPU utilization ratio](assets/dimensions-native-tcp-iterations0-cpu-ratio.svg)

- Standalone Asio: Matched coroutine/callback throughput median range: 0.92×–1.08×.
- Standalone Asio: Matched coroutine/callback CPU median range: 0.98×–1.02×; below 1 means lower average process CPU. Compare throughput.
- Boost.Asio: Matched coroutine/callback throughput median range: 0.92×–1.11×.
- Boost.Asio: Matched coroutine/callback CPU median range: 0.92×–1.05×; below 1 means lower average process CPU. Compare throughput.

## 10,000 deterministic iterations per echo

![Round-trip throughput](assets/dimensions-native-tcp-iterations10000-throughput.svg)

![Bidirectional application payload throughput](assets/dimensions-native-tcp-iterations10000-payload.svg)

![Batch p99 RTT](assets/dimensions-native-tcp-iterations10000-latency.svg)

![Average process CPU utilization](assets/dimensions-native-tcp-iterations10000-cpu.svg)

![Coroutine/callback throughput ratio](assets/dimensions-native-tcp-iterations10000-ratio.svg)

![Coroutine/callback CPU utilization ratio](assets/dimensions-native-tcp-iterations10000-cpu-ratio.svg)

- Standalone Asio: Matched coroutine/callback throughput median range: 0.95×–1.01×.
- Standalone Asio: Matched coroutine/callback CPU median range: 0.96×–1.01×; below 1 means lower average process CPU. Compare throughput.
- Boost.Asio: Matched coroutine/callback throughput median range: 0.93×–1.02×.
- Boost.Asio: Matched coroutine/callback CPU median range: 0.95×–1.01×; below 1 means lower average process CPU. Compare throughput.

[Complete numeric JSON (p95, run ranges and failures)](dimensions-summary.json ':ignore')

## Failed Profiles

No failed profiles.

<Aside type="caution" title="Failed profiles">

Any failed repetition excludes the profile’s three runs from rate, latency and matched-ratio summaries. Raw failures remain.

</Aside>

<Aside type="caution" title="Ratios and limits">

Pair repetitions with the same dependency combination, payload, batch, thread model and computation, then take throughput/CPU ratio medians; 1 means equal. Lower CPU need not complete more requests; compare throughput. Any missing CPU sample makes CPU summaries/ratios N/A; throughput may remain valid. This cannot predict arknet callback versus future coroutine API speed.

</Aside>

[Metrics, statistics and reproduction](methodology.md)
