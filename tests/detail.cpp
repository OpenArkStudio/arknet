#include <arknet/base/detail/ecs.hpp>
#include <arknet/base/detail/function.hpp>
#include <arknet/base/listener.hpp>
#include <arknet/base/detail/allocator.hpp>
#include <arknet/base/detail/linear_buffer.hpp>
#include <arknet/base/impl/event_queue_cp.hpp>
#include <arknet/base/impl/condition_event_cp.hpp>
#include <arknet/base/session_mgr.hpp>
#include "check.hpp"

struct test_option : arknet::detail::component_tag
{
    int value = 7;
};

DOCTEST_TEST_CASE("Move-only function wrappers replace, reset and remove themselves safely")
{
    arknet::detail::function<int(int)> handler([value = std::make_unique<int>(7)](int input)
                                               { return *value + input; });
    auto moved = std::move(handler);
    check(moved(5) == 12, "move-only callable");
    moved = [](int input) { return input * 2; };
    check(moved(5) == 10, "callable replacement");
    moved.reset();
    check(!moved, "empty callable");
    bool bad_call = false;
    try
    {
        moved(1);
    }
    catch (const std::bad_function_call&)
    {
        bad_call = true;
    }
    check(bad_call, "empty callable throws");

    std::unique_ptr<arknet::detail::function<int()>> self_removing;
    self_removing = std::make_unique<arknet::detail::function<int()>>(
        [value = std::make_unique<int>(11), &self_removing]
        {
            self_removing.reset();
            return *value;
        });
    check((*self_removing)() == 11 && !self_removing, "callable survives reentrant removal");
}

DOCTEST_TEST_CASE("Receive observers preserve lvalue session references")
{
    arknet::detail::listener_t listener;
    auto session = std::make_shared<int>(1);
    listener.bind(arknet::detail::event_type::recv,
                  arknet::detail::observer_t<std::shared_ptr<int>&, std::string_view>(
                      [](auto& owner, std::string_view data) { *owner += static_cast<int>(data.size()); }));
    std::string_view data = "hello";
    listener.notify(arknet::detail::event_type::recv, session, data);
    check(*session == 6, "lvalue receive observer signature");
}

DOCTEST_TEST_CASE("ECS clone owns independent delimiter and component state")
{
    auto ecs = arknet::detail::ecs_helper::make_ecs(asio::transfer_at_least(1), '\n', test_option{});
    auto copy = ecs->clone();
    check(copy.get_condition().lowest() == '\n', "cloned delimiter");
    auto original = std::get<0>(ecs->get_component().values());
    auto cloned = std::get<0>(copy.get_component().values());
    check(original != cloned, "component clone owns independent state");
    check(cloned->value == 7, "cloned component value");
    original->value = 9;
    check(cloned->value == 7, "original mutation must not change clone");
}

DOCTEST_TEST_CASE("Datagram matchers retain incomplete frames and find complete boundaries")
{
    for (auto size : {std::size_t(253), std::size_t(254), std::size_t(65536)})
    {
        const std::size_t header = size < 254 ? 1 : size <= 65535 ? 3 : 9;
        std::string wire(header + size, 'x');
        wire[0] = static_cast<char>(header == 1 ? size : header == 3 ? 254 : 255);
        for (std::size_t i = 1; i < header; ++i)
            wire[i] = static_cast<char>((std::uint64_t(size) >> ((i - 1) * 8)) & 0xff);

        asio::streambuf buffer;
        auto storage = buffer.prepare(wire.size());
        asio::buffer_copy(storage, asio::buffer(wire));
        buffer.commit(wire.size());
        const auto buffer_data = buffer.data();
        auto begin = asio::buffers_begin(buffer_data);
        auto end = asio::buffers_end(buffer_data);
        auto incomplete = arknet::detail::dgram_match_role(begin, end - 1);
        check(!incomplete.second, "incomplete datagram remains buffered");
        auto complete = arknet::detail::dgram_match_role(begin, end);
        check(complete.second && complete.first == end, "complete datagram boundary");
    }
}

DOCTEST_TEST_CASE("Datagram matchers reject invalid length headers")
{
    asio::streambuf malformed;
    std::string wire(9, static_cast<char>(255));
    auto storage = malformed.prepare(wire.size());
    asio::buffer_copy(storage, asio::buffer(wire));
    malformed.commit(wire.size());
    const auto malformed_data = malformed.data();
    auto begin = asio::buffers_begin(malformed_data);
    auto result = arknet::detail::dgram_match_role(begin, asio::buffers_end(malformed_data));
    check(result.second && result.first == begin, "invalid datagram length rejected");
}

DOCTEST_TEST_CASE("Linear buffers enforce limits after compaction and capacity reuse")
{
    arknet::linear_buffer buffer(16);
    asio::buffer_copy(buffer.prepare(8), asio::buffer("abcdefgh", 8));
    buffer.commit(8);
    buffer.consume(6);
    asio::buffer_copy(buffer.prepare(14), asio::buffer("ijklmnopqrstuv", 14));
    buffer.commit(14);
    const auto data = buffer.data();
    check(std::string_view(static_cast<const char*>(data.data()), data.size()) == "ghijklmnopqrstuv",
          "compaction preserves unread data");
    bool bounded = false;
    try
    {
        buffer.prepare(1);
    }
    catch (const std::length_error&)
    {
        bounded = true;
    }
    check(bounded && buffer.size() == 16, "full buffer rejects another byte");
    buffer.consume(16);
    check(buffer.capacity() >= 16, "capacity retained for reuse");
    bounded = false;
    try
    {
        buffer.prepare(17);
    }
    catch (const std::length_error&)
    {
        bounded = true;
    }
    check(bounded && buffer.size() == 0, "retained capacity does not bypass the limit");
    buffer.prepare(16);
    buffer.commit(100);
    check(buffer.size() == 16, "commit is bounded by prepared bytes");
}

DOCTEST_TEST_CASE("Handler allocators use reusable storage and aligned heap fallback")
{
    using memory_type = arknet::detail::handler_memory<std::false_type, arknet::detail::allocator_fixed_size_op<64>>;
    memory_type memory;
    void* first = memory.allocate(1, 1);
    void* second = memory.allocate(1, 1);
    check(first != second, "only one allocation occupies local storage");
    memory.deallocate(second, 1);
    memory.deallocate(first, 1);
    void* reused = memory.allocate(1, 1);
    check(reused == first, "local storage reused after release");
    memory.deallocate(reused, 1);
    struct alignas(64) aligned_value
    {
        std::array<std::byte, 64> bytes{};
    };
    arknet::detail::handler_allocator<aligned_value, std::false_type, arknet::detail::allocator_fixed_size_op<64>>
        allocator(memory);
    auto* aligned = allocator.allocate(2);
    check(reinterpret_cast<std::uintptr_t>(aligned) % alignof(aligned_value) == 0, "overaligned heap allocation");
    allocator.deallocate(aligned, 2);
    bool overflow = false;
    try
    {
        allocator.allocate(std::numeric_limits<std::size_t>::max());
    }
    catch (const std::bad_array_new_length&)
    {
        overflow = true;
    }
    check(overflow, "allocator rejects multiplication overflow");
}

DOCTEST_TEST_CASE("Listeners can replace and clear themselves during notification")
{
    using namespace arknet::detail;
    listener_t listener;
    int value = 0;
    listener.bind(event_type::start, observer_t<>(
                                         [&]
                                         {
                                             ++value;
                                             listener.bind(event_type::start, observer_t<>(
                                                                                  [&]
                                                                                  {
                                                                                      value += 10;
                                                                                      listener.clear();
                                                                                  }));
                                         }));
    listener.notify(event_type::start);
    listener.notify(event_type::start);
    listener.notify(event_type::start);
    check(value == 11, "listener replacement and removal remain valid");
    struct receiver
    {
        int value = 0;
        void receive(int increment) { value += increment; }
    } target;
    observer_t<int> observer(&receiver::receive, std::ref(target));
    observer(3);
    check(target.value == 3, "member binding supports explicit reference ownership");
    observer_t<> stateful([count = 0, &value]() mutable { value = ++count; });
    stateful();
    stateful();
    check(value == 2, "mutable callback state persists between notifications");
    auto independent = stateful;
    stateful();
    stateful();
    independent();
    check(value == 3, "observer copies own independent mutable callback state");
}

struct queue_fixture : arknet::detail::event_queue_cp<queue_fixture>
{
    explicit queue_fixture(std::shared_ptr<asio::io_context> context)
        : io_(std::make_shared<arknet::detail::io_t>(std::move(context)))
    {
    }
    std::shared_ptr<queue_fixture> selfptr() const { return {}; }
    auto& wallocator() { return allocator_; }
    using event_queue_cp::next_event;
    using event_queue_cp::post_event;
    using event_queue_cp::push_event;
    std::shared_ptr<arknet::detail::io_t> io_;
    arknet::detail::handler_memory<> allocator_;
};

DOCTEST_TEST_CASE("Event queues drain long synchronous completion chains without recursion")
{
    auto context = std::make_shared<asio::io_context>();
    queue_fixture queue(context);
    std::size_t completed = 0;
    queue.post_event(
        [&](arknet::detail::event_queue_guard<queue_fixture>)
        {
            for (std::size_t i = 0; i < 50000; ++i)
                queue.push_event([&](arknet::detail::event_queue_guard<queue_fixture>) { ++completed; });
        });
    context->run();
    check(completed == 50000, "all synchronous queue entries complete");
}

struct managed_session_fixture
{
    using args_type = void;
    using key_type = int;
    explicit managed_session_fixture(int key) : key(key) {}
    int hash_key() const { return key; }
    int key;
};

DOCTEST_TEST_CASE("Session registries keep the active session when duplicate candidates close")
{
    auto context = std::make_shared<asio::io_context>();
    auto lane = std::make_shared<arknet::detail::io_t>(context);
    std::atomic state{arknet::detail::state_t::started};
    arknet::detail::session_mgr_t<managed_session_fixture> sessions(lane, state);
    auto active = std::make_shared<managed_session_fixture>(7);
    auto duplicate = std::make_shared<managed_session_fixture>(7);
    bool first_inserted = false, duplicate_inserted = true, duplicate_erased = true;
    sessions.emplace(active, [&](bool inserted) { first_inserted = inserted; });
    sessions.emplace(duplicate, [&](bool inserted) { duplicate_inserted = inserted; });
    sessions.erase(duplicate, [&](bool erased) { duplicate_erased = erased; });
    context->run();
    check(first_inserted && !duplicate_inserted && !duplicate_erased, "duplicate insertion and erasure rejected");
    check(sessions.find(7) == active && sessions.size() == 1, "original session remains registered");
    context->restart();
    bool active_erased = false;
    sessions.erase(active, [&](bool erased) { active_erased = erased; });
    context->run();
    check(active_erased && sessions.empty(), "active session can still be removed");
}

struct condition_fixture : arknet::condition_event
{
    using condition_event::async_wait;
    using condition_event::condition_event;
};

DOCTEST_TEST_CASE("Condition events remember notification before their asynchronous wait starts")
{
    auto context = std::make_shared<asio::io_context>();
    auto lane = std::make_shared<arknet::detail::io_t>(context, true);
    auto event = std::make_shared<condition_fixture>(lane);
    std::size_t calls = 0;
    event->notify();
    asio::post(lane->executor(), [&] { event->async_wait([&] { ++calls; }); });
    context->run();
    check(calls == 1 && lane->timers().empty(), "early notification completes exactly once");
}
