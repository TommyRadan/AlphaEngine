// Unit tests for core::event_bus: synchronous emit, type keying, buffered
// enqueue/flush ordering, and deferral of events enqueued during a flush.

#include <gtest/gtest.h>

#include <functional>
#include <vector>

#include <core/event_engine.hpp>

namespace
{
    struct event_a
    {
        int value{0};
    };

    struct event_b
    {
        int value{0};
    };
}

TEST(event_bus, emit_invokes_subscribed_listener)
{
    core::event_bus bus;
    int seen = 0;
    bus.subscribe<event_a>([&](const event_a& e) { seen = e.value; });
    bus.emit<event_a>(7);
    EXPECT_EQ(seen, 7);
}

TEST(event_bus, emit_with_no_listener_is_a_noop)
{
    core::event_bus bus;
    EXPECT_NO_THROW(bus.emit<event_a>(1));
}

TEST(event_bus, every_listener_for_a_type_is_invoked)
{
    core::event_bus bus;
    int a = 0;
    int b = 0;
    bus.subscribe<event_a>([&](const event_a& e) { a = e.value; });
    bus.subscribe<event_a>([&](const event_a& e) { b = e.value + 1; });
    bus.emit<event_a>(10);
    EXPECT_EQ(a, 10);
    EXPECT_EQ(b, 11);
}

TEST(event_bus, listeners_are_keyed_by_event_type)
{
    core::event_bus bus;
    int a_calls = 0;
    int b_calls = 0;
    bus.subscribe<event_a>([&](const event_a&) { ++a_calls; });
    bus.subscribe<event_b>([&](const event_b&) { ++b_calls; });

    bus.emit<event_a>(0);
    EXPECT_EQ(a_calls, 1);
    EXPECT_EQ(b_calls, 0); // an event_a must not reach an event_b listener

    bus.emit<event_b>(0);
    EXPECT_EQ(a_calls, 1);
    EXPECT_EQ(b_calls, 1);
}

TEST(event_bus, null_listener_is_ignored)
{
    core::event_bus bus;
    bus.subscribe<event_a>(std::function<void(const event_a&)>{});
    EXPECT_NO_THROW(bus.emit<event_a>(1));
}

TEST(event_bus, enqueued_events_are_not_delivered_until_flush)
{
    core::event_bus bus;
    int seen = 0;
    bus.subscribe<event_a>([&](const event_a& e) { seen = e.value; });
    bus.enqueue<event_a>(99);
    EXPECT_EQ(seen, 0); // still buffered
    bus.flush();
    EXPECT_EQ(seen, 99);
}

TEST(event_bus, flush_delivers_in_fifo_order)
{
    core::event_bus bus;
    std::vector<int> order;
    bus.subscribe<event_a>([&](const event_a& e) { order.push_back(e.value); });
    bus.enqueue<event_a>(1);
    bus.enqueue<event_a>(2);
    bus.enqueue<event_a>(3);
    bus.flush();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(event_bus, second_flush_has_nothing_to_deliver)
{
    core::event_bus bus;
    int calls = 0;
    bus.subscribe<event_a>([&](const event_a&) { ++calls; });
    bus.enqueue<event_a>(1);
    bus.flush();
    bus.flush();
    EXPECT_EQ(calls, 1);
}

TEST(event_bus, events_enqueued_during_flush_are_deferred_to_next_flush)
{
    core::event_bus bus;
    int calls = 0;
    // Re-enqueue exactly once, on the first delivery, to avoid an infinite loop.
    bus.subscribe<event_a>(
        [&](const event_a& e)
        {
            ++calls;
            if (e.value == 0)
            {
                bus.enqueue<event_a>(1);
            }
        });
    bus.enqueue<event_a>(0);

    bus.flush(); // delivers value 0; the value-1 event is deferred
    EXPECT_EQ(calls, 1);

    bus.flush(); // now delivers the deferred value-1 event
    EXPECT_EQ(calls, 2);
}
