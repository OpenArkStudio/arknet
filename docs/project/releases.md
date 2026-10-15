# Releases

## Next release preparation

No version or release date is assigned to this candidate. The published release
remains `0.1.0`; the following items are release gates, not available additions.

The candidate scope includes native single-node MySQL and Redis clients and a
dependency simplification that would default to Boost.Asio/Beast and remove BHO.
Standalone Asio would retain TCP, UDP, RUDP and TLS. The current defaults and
available protocols in [installation](../get-started/installation.md) remain in
effect until those changes are merged and validated.

| Release gate | Required completion |
| --- | --- |
| MySQL client | Accept the [agreed query, prepared-statement and transaction scope](../features/database-clients.md), authentication/TLS, pools, limits, cancellation and independent-server interoperability |
| Redis client | Accept RESP2/RESP3 commands and pipelines, authentication/TLS, pools, limits, cancellation and correct reply correlation against an independent server |
| Dependency change | Resolve the pending Windows failure diagnosis in [PR #6](https://github.com/OpenArkStudio/arknet/pull/6), integrate current IO lifecycle fixes, then validate the protocol/dependency/TLS build matrix and installed consumers |
| Validation record | Record exact source revisions, server/machine configurations, correctness and failure-path results, concurrency checks and reproducible local performance measurements |
| Public documentation | Provide compilable client programs, the tested API reference, supported server versions and authentication methods, and clear limitations in both languages |

Codec-only work does not satisfy the client release gates. After all candidate
changes are accepted through PRs and required checks, select the version, update
version metadata and publish the final release notes. The scope above does not
claim deployment capacity; [production acceptance](production-readiness.md)
remains workload-specific.

## 0.1.0

The first regular release includes TCP, native UDP, RUDP, WebSocket/WSS,
HTTP/HTTPS with routing, and optional TLS/mTLS. arknet remains C++20 and
header-only, with Standalone Asio and Boost.Asio dependency configurations.

RUDP includes ARQ-only, fixed Reed–Solomon FEC and adaptive FEC policies.
Negotiation, ordered delivery, recovery, bounded resources, reconnect and
legacy zephyr interoperability have passed local checks. Single HTTP requests
include deadlines and cancellation.

Lifecycle status queries use atomic state, avoiding races with socket and
acceptor shutdown. A permanent regression polls clients, servers and retained
sessions while their IO thread starts and closes transports.

The [testing record](testing.md) and [RUDP performance report](../performance/rudp.md)
describe the verified workloads and source revisions. A library release does
not establish a deployment's capacity: cross-host load, production-duration
endurance and ARK business regression remain [deployment acceptance](production-readiness.md).
Cluster capacity of 100,000–200,000 PCU has not been measured.

Changes to `main` require a pull request, passing required CI checks and resolved
review conversations. Administrators follow the same protection. Contributions
are squash-merged with English commit subjects; another maintainer's approval
is currently optional.

[GitHub releases](https://github.com/OpenArkStudio/arknet/releases)
