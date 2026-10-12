# IPv4 and IPv6

TCP, UDP, RUDP, HTTP/HTTPS, WebSocket/WSS and TLS accept IPv4 and IPv6 addresses
through the same `start(host, port)` interface. UDP cast also accepts IPv6 endpoints.

| Use | IPv4 | IPv6 |
| --- | --- | --- |
| Loopback bind/connect | `127.0.0.1` | `::1` |
| Listen on all interfaces | `0.0.0.0` | `::` |
| HTTP Host / WebSocket authority | `127.0.0.1:8080` | `[::1]:8080` |
| URL | `http://127.0.0.1:8080/` | `http://[::1]:8080/` |

Pass an IPv6 literal without brackets to `start`; the port is a separate argument.
HTTP requests set their own `Host` field. WebSocket clients construct the handshake
authority. DNS names may resolve to either family; `localhost` does not force IPv6.

## Domain Names and Resolution

Pass a domain name as `host`, with the port supplied separately:

```cpp
arknet::tcp_client client;
if (!client.start("example.com", 9000))
    std::cerr << arknet::last_error_msg() << '\n';
```

The client uses Asio's asynchronous resolver and the operating system's name
resolution settings, including DNS and the hosts file. Results may contain IPv4
or IPv6 addresses. TCP-based clients try addresses in resolver order after a
socket connection failure; they do not race IPv4 and IPv6 connections (Happy
Eyeballs). UDP `connect` only selects a peer address and does not verify peer
reachability; UDP and RUDP do not provide reachability-based address fallback.

An Asio `endpoint` stores a numeric IP address and port, not a domain name.
`arknet::to_endpoint<asio::ip::tcp::endpoint>(host, port)` resolves synchronously
and returns the first address; check `arknet::get_last_error()` before using it.
UDP cast's `async_send(host, port, data)` resolves asynchronously and selects
the first address matching the socket's IPv4/IPv6 family.

HTTPS and WSS retain the domain name for TLS SNI and certificate identity checks.
Do not replace it with a resolved IP unless the certificate authenticates that IP.
An established connection retains its peer address; reconnect starts resolution
again. There is no arknet API for selecting a custom DNS server.

## Client and Server

The complete [IPv6 echo example](https://github.com/OpenArkStudio/arknet/blob/main/examples/ipv6_echo.cpp)
starts both endpoints on `::1`, checks the reply and stops them. Build and run it:

```sh
cmake --build build --target arknet_ipv6_echo
build/examples/arknet_ipv6_echo
```

For the [TCP](../features/tcp/index.md), [UDP](../features/udp/index.md),
[WebSocket](../features/websocket/index.md) and [HTTP](../features/http/index.md) client/server examples,
replace both loopback addresses with `::1`. An HTTP request uses:

```cpp
request.set(arknet::http::field::host, "[::1]:8080");
```

TLS clients verify literal addresses against certificate IP subject alternative
names, including the full IPv6 address. A certificate for `localhost` or
`127.0.0.1` alone cannot authenticate `::1`. See [TLS identity](../features/tls/index.md).
The separate `tests/certs/ipv6` certificates are only for tests.

## Listening Policy

Binding `::` exposes all IPv6 interfaces. IPv4-mapped connections depend on the
platform's `IPV6_V6ONLY` policy; binding `::` alone does not guarantee a dual-stack
service. To support both families explicitly, use separate listeners on
`0.0.0.0` and `::`, with the IPv6 socket configured as IPv6-only.
Socket options can be set in `bind_init`, after the socket opens and before bind:

```cpp
server.bind_init([&]
{
    arknet::error_code error;
    server.acceptor().set_option(asio::ip::v6_only(true), error);
    if (error) throw arknet::system_error(error);
});
```

Only bind externally reachable interfaces when the application intends to accept
network traffic. UDP multicast configuration is family-specific; IPv4 broadcast
has no IPv6 equivalent. Link-local IPv6 addresses also require an interface scope.

## Tests and Benchmarks

The independent doctest suite is `build/tests/arknet_ipv6_test`.
Both C++ benchmarks and the shell matrix accept `--address-family ipv6`:

```sh
build/benchmarks/arknet_loopback_benchmark --protocol udp --address-family ipv6 \
  --payload 1024 --clients 16 --window 1 --io-model shared --io-threads 1 \
  --seconds 1 --warmup 0.25
build/benchmarks/arknet_loopback_benchmark --protocol https --address-family ipv6 \
  --certs tests/certs/ipv6 --payload 1024 --clients 16 --window 1 \
  --io-model shared --io-threads 1 --seconds 1 --warmup 0.25
build/benchmarks/arknet_coroutine_benchmark --protocol tcp --execution coroutine \
  --address-family ipv6 --payload 1024 --clients 16 --window 16 \
  --io-model shared --io-threads 4 --seconds 1 --warmup 0.25
```

Existing performance matrices use IPv4 loopback. IPv6 validation on `::1` does
not establish cross-host throughput, IPv4/IPv6 performance parity, multicast
behavior or dual-stack behavior on every platform.
