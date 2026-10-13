# Releases

## 0.1.0

The first regular release includes TCP, native UDP, RUDP, WebSocket/WSS,
HTTP/HTTPS with routing, and optional TLS/mTLS. arknet remains C++20 and
header-only, with Standalone Asio and Boost.Asio backends.

RUDP includes ARQ-only, fixed Reed–Solomon FEC and adaptive FEC policies.
Negotiation, ordered delivery, recovery, bounded resources, reconnect and
legacy zephyr interoperability have passed local checks. Single HTTP requests
include deadlines and cancellation.

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
