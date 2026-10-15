# Database clients

The planned MySQL and Redis clients connect applications to independent database
servers. The first version targets single-node deployments, with native protocol
implementations in arknet and framework integration in ark plugins.

<Aside type="note" title="Development status">
MySQL and Redis clients are not part of the published API. Their scope is agreed;
connection, pool and independent-server acceptance must finish before release.
Protocol codec tests cover only one part of that acceptance. Usage programs and
API reference will accompany the validated clients.
</Aside>

## First-version scope

| Client | Planned operations |
| --- | --- |
| MySQL | Queries, parameterized prepared statements and transactions |
| Redis | Generic commands, RESP2/RESP3 and pipelining |
| Both | Authentication, TLS, connection pools, timeouts, cancellation and bounded queues |

Redis Cluster, Sentinel and Pub/Sub remain future work. Supported server versions,
authentication methods, build dependencies and API names will be recorded with
the accepted implementation. The clients will be implemented in arknet, without
Boost.MySQL or Boost.Redis. The C++ implementation remains header-only; the
existing TLS facilities use OpenSSL.

## Library and application responsibilities

| Layer | Responsibility |
| --- | --- |
| arknet | Protocol framing and parsing, connection IO, authentication, TLS, reply correlation, pools and shutdown |
| ark database plugin | Framework configuration, module services and business callback integration |
| Application | Queries and commands, transaction boundaries, retry decisions and handling uncertain write outcomes |

Clients first pass acceptance in arknet. The ark plugins can then replace their
existing database drivers directly, with application regression tests.

## MySQL connection and transaction behavior

The intended flow is connection and authentication, pool acquisition, query or
prepared-statement execution, and connection return after the reply is consumed.
A prepared statement belongs to its connection. A transaction keeps the same
pool lease through commit or rollback; the pool must not lend that connection
to another operation in the meantime.

An interrupted operation must reach a known protocol boundary or discard its
connection before reuse. If the connection fails after a write or commit was
sent, the result may be unknown. Report that outcome to the caller; never
automatically replay the operation.

## Redis command and pipeline behavior

The intended flow is connection and authentication, command admission, ordered
reply consumption, and completion delivery. Pipeline commands share a connection;
their replies must remain associated with the commands that produced them.
RESP3 push messages must not consume ordinary command completion slots.

A queued command can be cancelled before it is sent. Once a command has been
sent, cancellation or timeout must still consume its reply or close the
connection before it serves later commands. A write without a definitive reply
must retain an uncertain-outcome result rather than be replayed automatically.

## Acceptance before release

| Area | Required evidence |
| --- | --- |
| Codecs | Fragmented input, combined replies, binary values, malformed lengths, nesting and memory limits |
| Connections | Independent MySQL/Redis servers; authentication, TLS certificate and hostname verification, disconnects and reply ordering |
| Operations | MySQL query, prepare/execute and transaction results; Redis RESP2/RESP3 commands and pipelines |
| Cancellation | Unsent cancellation, in-flight timeout, completion exactly once, and correct handling of the next reply |
| Pools | Acquisition timeout, queue rejection, connection-bound transactions/statements, and shutdown with queued or in-flight work |
| Concurrency | Supported IO models, shared pool access and separate ASan/UBSan and TSan checks |
| Sustained load | Completion counts, bounded queue/pool memory and resource recovery over a recorded duration |

Record the actual server versions and configuration for interoperability tests.
Test authentication and TLS failures, server restart, partial replies, queue
saturation and shutdown as well as successful requests. A codec test result does
not complete the connection or pool acceptance rows.

ark plugin integration follows client acceptance. It separately verifies existing
plugin calls, configuration, business results and module shutdown after direct
driver replacement.

## Performance reports

Database client measurements have not been published. Add compilable C++
benchmarks under `benchmarks/` before publishing results; shell scripts can run
measurement batches without introducing a Python or Ruby requirement.

| Dimension | Report content |
| --- | --- |
| Environment | Client/server machines, operating systems, database versions/configuration, compiler, build mode and dependency versions |
| Workload | Operation and payload sizes, connections, pool size, in-flight work and Redis pipeline depth |
| Execution | One IO context/one thread, one context/multiple threads and one context per thread; callback/coroutine comparisons only when both APIs exist |
| Results | Successful operations/s, p50/p99 end-to-end operation latency, client/server CPU utilization, memory, queue rejections and errors |
| Reproducibility | Committed benchmark program and command, repeated process runs, duration and raw measurements supporting the final report |

Keep database-server cost separate from client cost and record whether traffic
uses loopback or separate hosts. State the CPU percentage normalization and use
labeled SVG charts with units and measurement variation for final comparisons.
Deployment capacity requires its own
[production acceptance](../project/production-readiness.md).

[Roadmap](../project/roadmap.md) · [Release preparation](../project/releases.md) · [Testing](../project/testing.md)
