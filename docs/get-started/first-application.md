# First Client and Server

This complete program starts a loopback TCP echo server and a client, verifies
a reply, and shuts both down.

<Steps>

<Step title="Create the application project">

Follow [installation](installation.md) to install dependencies. Create `CMakeLists.txt`
next to `main.cpp`, replacing `/path/to/arknet` with your checkout path:

```cmake
cmake_minimum_required(VERSION 3.21)
project(arknet_echo LANGUAGES CXX)
add_subdirectory(/path/to/arknet arknet-build)
add_executable(arknet_echo main.cpp)
target_link_libraries(arknet_echo PRIVATE arknet::arknet)
```

arknet is header-only; the interface target propagates build settings and dependencies.

</Step>

<Step title="Add the client and server">

Put the complete program below in `main.cpp`:

```cpp
#include <chrono>
#include <future>
#include <string>

#include <arknet/arknet.hpp>
using namespace std::chrono_literals;

int main()
{
    arknet::tcp_server server;
    server.bind_recv([](auto& session, std::string_view data)
    {
        session->async_send(data);
    });
    if (!server.start("127.0.0.1", 0, arknet::use_dgram))
        return 1;

    arknet::tcp_client client;
    client.set_auto_reconnect(false);
    std::promise<std::string> reply;
    client.bind_recv([&](std::string_view data)
    {
        reply.set_value(std::string(data));
    });
    if (!client.start("127.0.0.1", server.get_listen_port(), arknet::use_dgram))
        return 2;
    auto result = reply.get_future();
    if (!client.async_send("hello"))
        return 3;
    const bool ready = result.wait_for(5s) == std::future_status::ready;
    const bool correct = ready && result.get() == "hello";
    client.stop();
    server.stop();
    return correct ? 0 : 4;
}
```

</Step>

<Step title="Build and check the echo">

```sh
cmake -S . -B build
cmake --build build --parallel 3
./build/arknet_echo
```

Exit status `0` confirms that the client received `hello` and both endpoints stopped.
With Visual Studio, build with `--config Release` and run `build/Release/arknet_echo.exe`.

</Step>

</Steps>

<Aside type="note" title="Note: TCP message boundaries">

Both peers use arknet's `use_dgram` message framing. TCP itself is a byte
stream: choose [framing](../features/tcp/index.md) for the peer's wire protocol. The example
waits on the owner thread, outside IO callbacks.

</Aside>

## Next Steps

<CardGrid>

<LinkCard title="TCP" href="../features/tcp/index.md" description="Ordered byte streams." />
<LinkCard title="UDP" href="../features/udp/index.md" description="Native datagrams." />
<LinkCard title="WebSocket" href="../features/websocket/index.md" description="Complete messages." />
<LinkCard title="HTTP and routing" href="../features/http/index.md" description="Requests, responses and middleware." />
<LinkCard title="TLS" href="../features/tls/index.md" description="Encrypted transport." />

</CardGrid>

<CardGrid>

<LinkCard title="Lifecycle and sends" href="../concepts/sending.md" description="Read before keeping objects in a service." />
<LinkCard title="IO models" href="../concepts/io-models.md" description="Read before sharing a context." />

</CardGrid>
