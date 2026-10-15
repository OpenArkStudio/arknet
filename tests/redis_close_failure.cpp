// Copyright (c) 2026 OpenArkStudio. Distributed under BSL-1.0. See LICENSE.
#if defined(_MSC_VER)
#include "scoped_asio_allocation_failure.hpp"

#endif

#include <future>
#include <new>

#include "redis_test_peer.hpp"
#include "scoped_allocation_failure.hpp"

DOCTEST_TEST_CASE("Redis close retries a real Asio dispatch allocation failure before actor acceptance") {
    using namespace redis_test;
    std::promise<void> peer_closed;
    auto closed = peer_closed.get_future();
    server endpoint([&](peer& remote) {
        remote.hello();
        remote.expect({"PING"});
        remote.send("+PONG\r\n");
        remote.wait_closed();
        peer_closed.set_value();
    });
    client connection;
    REQUIRE(bool(connect(connection, endpoint.port())));
    bool threw{};
    const auto before = scoped_allocation_failure::injections.load();
    {
        scoped_allocation_failure::scope failure;
        try
        { connection.close(); }
        catch (const std::bad_alloc&)
        { threw = true; }
    }
    CHECK(threw);
    CHECK(scoped_allocation_failure::injections == before + 1);
    CHECK(connection.is_connected());
    std::promise<result> completed;
    auto future = completed.get_future();
    connection.async_execute({"PING"}, [&](result response) { completed.set_value(std::move(response)); });
    auto response = await(future);
    CHECK(bool(response));
    CHECK(response.replies.size() == 1);
    if (response.replies.size() == 1)
    {
        CHECK(response.replies.front().text == "PONG");
    }
    connection.close();
    CHECK(closed.wait_for(1s) == std::future_status::ready);
    CHECK(connection.close_and_wait());
    endpoint.finish();
}
