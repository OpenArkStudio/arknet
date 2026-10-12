# Create a RUDP server

Follow [installation](../../get-started/installation.md), then build this program
and the [client](client.md) as separate executables. Start the server first.

```cpp
#include <iostream>
#include <string_view>

#include <arknet/rudp/rudp_server.hpp>

int main()
{
    arknet::rudp_server server;
    server.bind_recv([](auto& session, std::string_view message)
    {
        session->async_send(message);
    });
    if (!server.start("127.0.0.1", 7000))
    {
        std::cerr << arknet::get_last_error().message() << '\n';
        return 1;
    }
    std::cout << "RUDP listening on 127.0.0.1:7000\n";
    std::cin.get();
    server.stop();
}
```

`bind_recv` receives one complete application message. Its view is valid only
during the callback; `async_send` copies the message into owned storage.

Bind `bind_handshake([](auto& session, arknet::error_code error) { ... })`
to observe session setup. `bind_connect` and `bind_disconnect` receive the
session reference. Configure `set_rudp_options` before starting. New sessions
inherit these options. For IPv6, use `::1` on both ends.

The UDP socket is shared by sessions. Read [configuration](configuration.md)
for queue limits, close notices and the external-context lifetime contract.
