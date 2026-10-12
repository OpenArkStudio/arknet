# Optional FEC design

<Aside type="note" title="Note: Planned capability (TODO)">

This page records the agreed direction, not an available API.
The current RUDP implementation uses ARQ only. Its benchmarks do not measure FEC.

</Aside>

arknet is a general-purpose networking library. Games are one workload, alongside
control messages, service communication and bulk transfer. Presets should express
delivery requirements and resource budgets rather than an industry label.

## Composition

```text
Send:    application message -> ARQ datagrams -> optional FEC -> UDP
Receive: UDP -> FEC recovery -> ARQ ordering/reassembly -> application message
```

ARQ supplies ACKs, retransmission and ordered delivery. FEC can recover some
missing datagrams before retransmission. For example, ten data shards plus two
parity shards can recover up to two erasures in that group, with approximately
20% parity-payload overhead before headers and padding. Larger groups improve
coding efficiency but may add waiting time. Unrecovered loss still uses ARQ.

RUDP retains its reliable message contract. UDP with FEC alone can suit messages
that may be discarded after a deadline, but it cannot provide that contract.

## Candidate policies

The overall policy and shipped presets remain open for discussion and measurement.

| Candidate | Behavior | Suitable requirements |
| --- | --- | --- |
| ARQ only | No parity traffic | Reliable delivery with minimal added bandwidth and low loss |
| ARQ with adaptive FEC | Adjust parity using configured thresholds and budgets | Reliable delivery with a tight recovery-latency budget |
| ARQ with fixed FEC | Predictable coding parameters | A measured link with a known redundancy allowance |

Applications should configure activation conditions in advance. Runtime adaptation
must stay inside those limits and expose both the current parameters and the
reason for each change. No numeric thresholds are established yet.

## Configuration requirements

| Parameter group | Planned controls |
| --- | --- |
| Activation | Minimum sample count, observation window, loss/retransmission thresholds, recovery-latency target |
| Coding | Maximum data/parity shards, extra-byte budget, minimum useful group size |
| Latency | Maximum group wait, reorder grace period, group decoding deadline |
| Stability | Separate activation/deactivation thresholds, cooldown, stable windows before decreasing redundancy |
| Overload | Send-queue limits, RTT/queue growth guard, optional application-supplied load limit |
| Observability | Original datagrams, parity bytes, recovered/missing shards, ARQ retransmits, current parameters and transition reasons |

Loss measurement must distinguish reordering from missing datagrams and distinguish
FEC recovery from later ARQ retransmission. Insufficient samples retain the current
policy. Small or sparse groups must respect latency and byte budgets; when useful
coding cannot fit those budgets, send through ARQ without parity.

<Aside type="caution" title="Warning: Redundancy does not resolve overload">

Increasing redundancy is not a remedy for overload. RTT inflation and growing
queues require conservative rate control; neither signal proves the cause of
packet loss. ARQ congestion control remains active. CPU and bandwidth limits
must prevent adaptation from aggravating an already overloaded sender or receiver.

</Aside>

## Wire format and limits

Peers must select a FEC-capable wire mode at connection setup. Within that mode,
zero parity means redundancy is inactive; it does not switch the connection back
to bare KCP framing. Each group carries its actual coding parameters so the
receiver can decode while the sender changes future groups.

The format needs versioning, conversation/group/shard identifiers, lengths and
strict validation before allocation. FEC headers and padding count toward the
UDP MTU and memory limits. Group count, retained bytes and decoding lifetime
must be bounded. Control messages must not wait for a data group to fill.
KCP-compatible ARQ-only mode remains independently usable.

## Validation before defaults

Compare ARQ alone, fixed FEC and adaptive FEC under random and burst loss,
reordering, different RTTs, sparse and sustained traffic, and receive-queue
overflow. Measure delivery correctness, p50/p99 RTT, extra transmitted bytes,
CPU utilization and memory. Tests must also cover peer compatibility, malformed
groups, parameter transitions and shutdown with incomplete groups.

The coding engine, preset names and values, adaptation algorithm and negotiation
format are not selected. They will be decided after the current RUDP validation
and the dedicated loss experiments.
