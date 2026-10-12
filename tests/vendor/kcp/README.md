# KCP Test Reference

[简体中文](README_CN.md)

The interoperability tests compile the unmodified canonical C implementation
from [skywind3000/kcp](https://github.com/skywind3000/kcp), pinned to commit
[`b1a7a2101dcbb96017681a500d6b82bbe5a88766`](https://github.com/skywind3000/kcp/tree/b1a7a2101dcbb96017681a500d6b82bbe5a88766).

These files are a test oracle only. They are not installed or included by
arknet public headers. arknet's C++ protocol implementation does not depend on
this source or on the former zephyr integration.

The upstream MIT [LICENSE](LICENSE), copyright notices, and source line endings
are preserved. Git blob identifiers for the imported upstream files:

| File | Git Blob |
| --- | --- |
| `ikcp.c` | `1f32039d9ac6bc80164b3241bacf6622fa3a34e6` |
| `ikcp.h` | `a7447c16946b2f4fc78572b325d4b924401bb42c` |
| `LICENSE` | `4f9f28fdcb8cb5d988e684a4503fc2fad29ca2d5` |

To verify the preserved source bytes:

```sh
git hash-object --no-filters tests/vendor/kcp/ikcp.c tests/vendor/kcp/ikcp.h tests/vendor/kcp/LICENSE
```

`tests/arq_interop.cpp` runs bidirectional message delivery against this
reference with deterministic packet loss, duplication, and reordering. It also
checks ACK and zero-window WASK/WINS exchange. KCP's UDP session handshake is
outside the core wire protocol and is tested separately by the session tests.

The upstream `IOFFSETOF` macro computes container offsets using null-member
expressions. UBSan builds disable null and object-size checks for `ikcp.c`
because Clang's object-size instrumentation also reports these expressions.
All arknet code remains fully instrumented, and the other checks on the
reference remain enabled. This keeps the canonical test source unchanged.
