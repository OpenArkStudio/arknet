# zephyr / arknet RDP endpoint interoperability evidence

Date: 2026-10-13. Local macOS 27.0.1 (26A434), AppleClang 21.0.0.21000101, CMake Release builds.

| Backend | Process pairs | Sessions | Application messages | Echoes | Result |
| --- | ---: | ---: | ---: | ---: | --- |
| Standalone Asio 1.38.2 | 12 | 16 | 512 | 512 | Passed |
| Boost.Asio / Boost 1.90.0 | 12 | 16 | 512 | 512 | Passed |

Both real endpoint directions were executed in separate processes: zephyr UDP/KCP client to arknet RUDP server, and arknet RUDP client to zephyr UDP/KCP server. Each direction used both client-initiated and server-initiated close, under `baseline`, `reconnect`, and `impaired`. Reconnect restarts the same client against the same running server for two full sessions and resets the expected message index. Every session checks exact ordered bytes, one disconnect notification per endpoint, remote EOF when appropriate, complete shutdown, and an empty server session table.

Each session carries 32 binary messages with ARK-compatible native packed CS (7-byte) and SS (23-byte) headers. Body sizes include 0, 1, 23, 511, 1024, and 4096 bytes; content includes NUL and all byte values. Four application messages are in flight and endpoint/relay receive buffers request 1 MiB. arknet uses its default 1200-byte MTU, congestion control disabled for this integration check, and ARQ without FEC.

The test-only relay drops every ninth ARQ datagram in each direction and holds every seventh eligible ARQ datagram until another packet passes it or a 6 ms deadline. The relay preserves control packets. Actual weak-network counters from the successful runs:

| Backend | Direction | Close initiator | Dropped | Reordered | Socket errors | FIN forwarded |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| Standalone | zephyr client / arknet server | Client | 31 | 35 | 0 | 1 |
| Standalone | zephyr client / arknet server | Server | 30 | 32 | 0 | 1 |
| Standalone | arknet client / zephyr server | Client | 31 | 34 | 0 | 1 |
| Standalone | arknet client / zephyr server | Server | 32 | 34 | 0 | 1 |
| Boost | zephyr client / arknet server | Client | 30 | 32 | 0 | 1 |
| Boost | zephyr client / arknet server | Server | 32 | 35 | 0 | 1 |
| Boost | arknet client / zephyr server | Client | 32 | 37 | 0 | 1 |
| Boost | arknet client / zephyr server | Server | 32 | 37 | 0 | 1 |

## Evidence files

- `/private/tmp/arknet-interop-final-standalone-configure.log`
- `/private/tmp/arknet-interop-final-standalone-build-final.log`
- `/private/tmp/arknet-interop-final-standalone-run.log`
- `/private/tmp/arknet-interop-final-boost-configure.log`
- `/private/tmp/arknet-interop-final-boost-build.log`
- `/private/tmp/arknet-interop-final-boost-run.log`

Both build commands and both final integration target runs exited 0. Each run log contains 12 `Checking` entries and the successfully completed `arknet_zephyr_interop_check` target. `git diff --check` and clang-format 18.1.8 dry-run checks passed after the final source edits. No build or test sessions remain running in this agent.

## Test-only legacy compatibility

The supplied zephyr checkout was `/Users/nick/work/opensource/ARK/3rdparty/zephyr`; it was not modified. Generated build overlays apply the existing ARK resolver `.begin()` fix and qualify two inherited UDP client/session constructor declarations for Clang. `ASIO_NO_DEPRECATED` or `BOOST_ASIO_NO_DEPRECATED` disables legacy overloads for buffer types removed in modern Asio. Null/object-size sanitizer exclusions are restricted to the optional legacy target because its KCP reference uses null-member offsets. The legacy header produces an existing deprecated `vsprintf` warning; no warning suppression was added.

## Limits

SYN/ACK/FIN are preserved by this relay. FIN is a one-way notification with no acknowledgement or retransmission; if FIN is lost in an actual network, the peer can remain connected until timeout. The relay waits for observed FIN forwarding before shutting itself down, preventing the test process from suppressing a FIN still queued for forwarding. Passing the weak-network cases proves ARQ data recovery with delivered control packets, not reliable shutdown after control-packet loss. Separate `rudp` tests cover handshake retry loss. These mixed-peer tests do not exercise FEC, IPv6, ARK business behavior, cross-host deployment, or performance; those have separate coverage or require integration validation. Endpoint interoperability was run in Release builds, without ASan/TSan on the legacy pair.

Source SHA-256:

```text
b66653ca7655cae7db313b8e0be6f2d5c84d1ba5621234c81ca3ef8801a7fee4  tests/interop/rudp_peer.cpp
cce19a0928d4b44ea86749a66c62b25ba36b86df7abcb6cb8a7aaea3e7870836  tests/interop/impairment.hpp
97977ab11ec10fa01b918fa2763d873c27fcf38d2b7ce8899e410f8e92700da1  tests/interop/run.cmake
9e455e5f511fd22d228f5fa85eeb6f25ff74d4c24cb355e1948eec27ba3d7f6e  tests/zephyr_interop.cmake
```
