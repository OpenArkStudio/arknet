# RDP Endpoint Interoperability

[English](README.md) | [中文](README_CN.md)

These optional integration tests pair real zephyr UDP/KCP endpoints with arknet RUDP endpoints in separate processes. They do not vendor or install zephyr, and zephyr is not a library dependency or a default test dependency.

Provide the legacy checkout explicitly:

```sh
cmake -S . -B build/interop -DARKNET_BUILD_TESTS=ON \
  -DARKNET_ZEPHYR_ROOT=/path/to/zephyr
cmake --build build/interop --target arknet_zephyr_interop_check
```

Set `ARKNET_USE_BOOST_ASIO=ON` to use Boost.Asio for both peer executables. The default uses Standalone Asio. CMake generates compatibility overlays in the build directory for the old resolver result and inherited constructor declarations, and disables obsolete buffer overloads for the legacy target. The supplied zephyr checkout remains unchanged.

| Pair | Close initiated by |
| --- | --- |
| zephyr client / arknet server | Client and server, separately |
| arknet client / zephyr server | Client and server, separately |

Each pair runs three scenarios with both close initiators, for 12 process pairs:

| Scenario | Checks |
| --- | --- |
| `baseline` | One session, exact ordered delivery, remote FIN and session cleanup |
| `reconnect` | Restart the same client twice against the same server; reset the expected message index for each session and verify 64 total messages without duplicate delivery |
| `impaired` | Drop every ninth ARQ datagram in each direction; delay every seventh eligible datagram until another packet overtakes it, or for at most 6 ms; verify actual drops/reordering and exact ordered delivery |

Each session transfers 32 binary messages with ARK-compatible packed CS/SS headers, including 4096-byte bodies, embedded NUL bytes and byte values 0–255. Messages exceed the UDP wire MTU and exercise ARQ fragmentation. Every session checks one disconnect notification per endpoint.

The test uses four in-flight application messages and 1 MiB socket receive buffers to bound loopback bursts. A small test-only relay runs inside the arknet process for `impaired`; no relay runs in the other scenarios. It tests ARQ-only sessions; FEC is a separate opt-in protocol that zephyr does not implement. The legacy KCP header's null-member-offset idiom is excluded from null/object-size sanitizer checks, as in the existing reference KCP test.

The relay preserves SYN/ACK/FIN control packets and impairs ARQ datagrams only. FIN is one-way and has no acknowledgement or retransmission: loss can leave the remote peer connected until its timeout. These checks validate data recovery and shutdown with delivered control packets; they do not establish reliable shutdown over a lossy network. Handshake retry losses are covered by the separate `rudp` tests.

The two executables can also run directly:

```sh
build/interop/tests/arknet_rudp_interop_peer server client reconnect | \
  build/interop/tests/arknet_zephyr_rdp_peer client client reconnect
build/interop/tests/arknet_zephyr_rdp_peer server server impaired | \
  build/interop/tests/arknet_rudp_interop_peer client server impaired
```

The server publishes its ephemeral loopback port through stdout. The optional scenario argument defaults to `baseline`; both peers must select the same scenario and close initiator. CMake checks both process exit codes and gives each pair a 40-second deadline. ARK business integration requires separate validation.
