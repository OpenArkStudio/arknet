# Architecture and Extension

[API index](api.md) | [Runtime](runtime.md)

## Layers and Responsibilities

```text
Application callbacks / HTTP router / custom CRTP types
  TCP       UDP       HTTP/1       WebSocket
   |         |         |               |
   |         |       Beast           Beast
   +---------+---------+---------------+
      client / server / session lifecycle
      bounded sends, owned data, timers, reconnect
                     |
             io_pool / io_t lanes
          owned contexts or external strands
                     |
        standalone Asio or Boost.Asio
            OS sockets; optional OpenSSL
```

CRTP lets custom clients and sessions extend protocol behavior without an
extra virtual interface. Servers accept a session type. Components own
specific behavior: lifecycle, timers, reconnect, match conditions, event
sequencing and send admission/data ownership.

Standalone mode uses bundled BHO Beast adapted for standalone Asio. Boost
mode uses official Boost.Asio and Boost.Beast. Those are provider choices,
not interchangeable socket types within one program. HTTP/2 and HTTP/3
require distinct protocol engines; see the [roadmap](roadmap.md).

## Extend a Session

```cpp
struct application_session : arknet::tcp_session_t<application_session>
{
    using tcp_session_t::tcp_session_t;
    std::string tenant;
};
arknet::tcp_server_t<application_session> server;
server.bind_recv([](auto& session, std::string_view bytes)
{
    session->async_send(bytes);
});
```

Use a custom session for connection-local business state. Configuration shared
across sessions remains application-owned and needs synchronization when
different lanes can run concurrently. Choose framing to match the peer.
The other protocol families retain the same inheritance/composition pattern.

## Ownership and Execution

Accepted sends own their data and quota until one completion. Receive views
and message references last only through the callback. Each IO lane serializes
its endpoint state; independent lanes can execute concurrently. A strand is
an execution guarantee, not a fixed OS thread.

Network objects remain alive through shutdown. Callback code requests stop;
an owner waits before destruction. External contexts stay running until
network objects stop. Timers also drain real cancellation completions before
an external owner returns from stop. Read [runtime contracts](runtime.md)
and [IO models](threading.md) before changing these relationships.

## Adding a Capability

Keep transport parsing and business dispatch separate. New endpoints reuse
the client/session lifecycle and executor model, but define their own wire
parser, memory limits, cancellation and error contracts. Add public headers,
umbrella exports, provider/TLS build coverage, installed-consumer tests and
three guide pages: introduction, usage and performance.

Do not expose internal component APIs as a compatibility promise. Add a
public abstraction when a concrete protocol or user workflow needs it.
The [TODO list](roadmap.md) records future capabilities without empty API stubs.

## Validation

[Testing](testing.md) lists functional and sustained checks.
[Local performance charts](performance/overview.md) document the implementation
and hardware actually measured. Passing loopback tests does not establish WAN
behavior or production capacity. Third-party notices are in the
[third-party notices](THIRD_PARTY_NOTICES.md).
