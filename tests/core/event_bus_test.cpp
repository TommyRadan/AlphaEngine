// Unit tests for core::event_bus: synchronous emit, type keying, listener
// order, subscription tokens (RAII / reset / release / move / manual
// unsubscribe), reentrant subscribe / unsubscribe / emit from inside a
// dispatch, buffered enqueue/flush ordering, and deferral of events enqueued
// during a flush.

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <core/event_engine.hpp>
#include <core/subscription.hpp>

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

// --- synchronous emit -------------------------------------------------------

TEST(event_bus, emit_invokes_subscribed_listener)
{
    core::event_bus bus;
    int seen = 0;
    auto sub = bus.subscribe<event_a>([&](const event_a& e) { seen = e.value; });
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
    auto sub_a = bus.subscribe<event_a>([&](const event_a& e) { a = e.value; });
    auto sub_b = bus.subscribe<event_a>([&](const event_a& e) { b = e.value + 1; });
    bus.emit<event_a>(10);
    EXPECT_EQ(a, 10);
    EXPECT_EQ(b, 11);
}

TEST(event_bus, listeners_fire_in_subscription_order)
{
    core::event_bus bus;
    std::vector<int> order;
    auto first = bus.subscribe<event_a>([&](const event_a&) { order.push_back(1); });
    auto second = bus.subscribe<event_a>([&](const event_a&) { order.push_back(2); });
    auto third = bus.subscribe<event_a>([&](const event_a&) { order.push_back(3); });
    bus.emit<event_a>(0);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(event_bus, listeners_are_keyed_by_event_type)
{
    core::event_bus bus;
    int a_calls = 0;
    int b_calls = 0;
    auto sub_a = bus.subscribe<event_a>([&](const event_a&) { ++a_calls; });
    auto sub_b = bus.subscribe<event_b>([&](const event_b&) { ++b_calls; });

    bus.emit<event_a>(0);
    EXPECT_EQ(a_calls, 1);
    EXPECT_EQ(b_calls, 0); // an event_a must not reach an event_b listener

    bus.emit<event_b>(0);
    EXPECT_EQ(a_calls, 1);
    EXPECT_EQ(b_calls, 1);
}

TEST(event_bus, null_listener_is_ignored_and_yields_an_empty_token)
{
    core::event_bus bus;
    auto sub = bus.subscribe<event_a>(std::function<void(const event_a&)>{});
    EXPECT_FALSE(static_cast<bool>(sub));
    EXPECT_EQ(sub.id(), 0u);
    EXPECT_NO_THROW(bus.emit<event_a>(1));
}

// --- subscription tokens ----------------------------------------------------

TEST(event_bus, default_constructed_token_is_empty_and_inert)
{
    core::subscription sub;
    EXPECT_FALSE(static_cast<bool>(sub));
    EXPECT_EQ(sub.id(), 0u);
    EXPECT_NO_THROW(sub.reset());
    EXPECT_EQ(sub.release(), 0u);
}

TEST(event_bus, token_ids_are_non_zero_and_unique)
{
    core::event_bus bus;
    auto first = bus.subscribe<event_a>([](const event_a&) {});
    auto second = bus.subscribe<event_b>([](const event_b&) {});
    EXPECT_NE(first.id(), 0u);
    EXPECT_NE(second.id(), 0u);
    EXPECT_NE(first.id(), second.id());
}

TEST(event_bus, token_unsubscribes_on_destruction)
{
    core::event_bus bus;
    int calls = 0;
    {
        auto sub = bus.subscribe<event_a>([&](const event_a&) { ++calls; });
        bus.emit<event_a>(0);
        EXPECT_EQ(calls, 1);
    }
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 1); // the listener died with its token
}

TEST(event_bus, reset_unsubscribes_and_empties_the_token)
{
    core::event_bus bus;
    int calls = 0;
    auto sub = bus.subscribe<event_a>([&](const event_a&) { ++calls; });
    EXPECT_TRUE(static_cast<bool>(sub));

    sub.reset();
    EXPECT_FALSE(static_cast<bool>(sub));
    EXPECT_EQ(sub.id(), 0u);
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 0);

    EXPECT_NO_THROW(sub.reset()); // idempotent
}

TEST(event_bus, release_keeps_the_listener_and_hands_back_its_id)
{
    core::event_bus bus;
    int calls = 0;
    std::uint64_t id = 0;
    {
        auto sub = bus.subscribe<event_a>([&](const event_a&) { ++calls; });
        id = sub.release();
        EXPECT_FALSE(static_cast<bool>(sub));
    }
    EXPECT_NE(id, 0u);
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 1); // survived the token's destruction

    bus.unsubscribe(id); // manual removal still works with the released id
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 1);
}

TEST(event_bus, manual_unsubscribe_by_id_removes_only_that_listener)
{
    core::event_bus bus;
    int a = 0;
    int b = 0;
    auto sub_a = bus.subscribe<event_a>([&](const event_a&) { ++a; });
    auto sub_b = bus.subscribe<event_a>([&](const event_a&) { ++b; });

    bus.unsubscribe(sub_a.id());
    bus.emit<event_a>(0);
    EXPECT_EQ(a, 0);
    EXPECT_EQ(b, 1);

    // The token still holds the id; dropping it afterwards is harmless.
    EXPECT_NO_THROW(sub_a.reset());
}

TEST(event_bus, unsubscribe_of_an_unknown_id_is_a_noop)
{
    core::event_bus bus;
    int calls = 0;
    auto sub = bus.subscribe<event_a>([&](const event_a&) { ++calls; });
    EXPECT_NO_THROW(bus.unsubscribe(0));
    EXPECT_NO_THROW(bus.unsubscribe(sub.id() + 1000));
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 1);
}

TEST(event_bus, move_transfers_ownership_of_the_listener)
{
    core::event_bus bus;
    int calls = 0;
    auto source = bus.subscribe<event_a>([&](const event_a&) { ++calls; });
    const std::uint64_t id = source.id();

    core::subscription moved{std::move(source)};
    EXPECT_FALSE(static_cast<bool>(source));
    EXPECT_EQ(moved.id(), id);
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 1);

    core::subscription assigned;
    assigned = std::move(moved);
    EXPECT_FALSE(static_cast<bool>(moved));
    EXPECT_EQ(assigned.id(), id);
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 2);

    assigned.reset();
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 2);
}

TEST(event_bus, move_assignment_releases_the_previously_owned_listener)
{
    core::event_bus bus;
    int first = 0;
    int second = 0;
    auto sub = bus.subscribe<event_a>([&](const event_a&) { ++first; });
    sub = bus.subscribe<event_a>([&](const event_a&) { ++second; });
    bus.emit<event_a>(0);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

TEST(event_bus, token_outliving_its_bus_is_a_noop)
{
    core::subscription sub;
    {
        core::event_bus bus;
        sub = bus.subscribe<event_a>([](const event_a&) {});
        EXPECT_TRUE(static_cast<bool>(sub));
    }
    EXPECT_NO_THROW(sub.reset());
    EXPECT_FALSE(static_cast<bool>(sub));
}

TEST(event_bus, quit_drops_every_listener_and_leaves_tokens_harmless)
{
    core::event_bus bus;
    int calls = 0;
    auto sub = bus.subscribe<event_a>([&](const event_a&) { ++calls; });
    bus.quit();
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 0);
    EXPECT_NO_THROW(sub.reset());
}

TEST(event_bus, listener_captures_owning_tokens_are_safe_on_quit_and_destruction)
{
    // A listener whose captured state owns another token on the same bus:
    // dropping it (quit, or the bus's destructor) runs that token's
    // destructor while the registry is being torn down, which must not
    // re-enter it.
    int calls = 0;
    {
        core::event_bus bus;
        auto inner = std::make_shared<core::subscription>(
            bus.subscribe<event_a>([&](const event_a&) { ++calls; }));
        bus.subscribe<event_b>([inner](const event_b&) {}).release();
        inner.reset(); // the outer listener's capture now solely owns `inner`
        bus.quit();
        bus.emit<event_a>(0);
    }
    {
        core::event_bus bus;
        auto inner = std::make_shared<core::subscription>(
            bus.subscribe<event_a>([&](const event_a&) { ++calls; }));
        bus.subscribe<event_b>([inner](const event_b&) {}).release();
        inner.reset();
    } // destroyed without quit(): the capture dies with the registry
    EXPECT_EQ(calls, 0);
}

// --- reentrancy ---------------------------------------------------------------

TEST(event_bus, listener_subscribed_during_dispatch_fires_from_the_next_emit)
{
    core::event_bus bus;
    int late_calls = 0;
    core::subscription late;
    auto sub = bus.subscribe<event_a>(
        [&](const event_a&)
        {
            if (!late)
            {
                late = bus.subscribe<event_a>([&](const event_a&) { ++late_calls; });
            }
        });

    bus.emit<event_a>(0); // subscribes `late`; it must not fire for this emit
    EXPECT_TRUE(static_cast<bool>(late));
    EXPECT_EQ(late_calls, 0);

    bus.emit<event_a>(0);
    EXPECT_EQ(late_calls, 1);
}

TEST(event_bus, subscribing_to_another_type_during_dispatch_is_deferred_too)
{
    // Adding a new key could rehash the map under the walk; the add is held
    // back until the outermost dispatch returns, so a nested emit of the
    // new type from the same listener does not reach the new listener yet.
    core::event_bus bus;
    int b_calls = 0;
    core::subscription late_b;
    auto sub_a = bus.subscribe<event_a>(
        [&](const event_a&)
        {
            if (!late_b)
            {
                late_b = bus.subscribe<event_b>([&](const event_b&) { ++b_calls; });
                bus.emit<event_b>(0);
            }
        });

    bus.emit<event_a>(0);
    EXPECT_EQ(b_calls, 0);
    bus.emit<event_b>(0);
    EXPECT_EQ(b_calls, 1);
}

TEST(event_bus, listener_unsubscribed_during_dispatch_is_skipped_for_that_dispatch)
{
    core::event_bus bus;
    int second_calls = 0;
    core::subscription second;
    auto first = bus.subscribe<event_a>([&](const event_a&) { second.reset(); });
    second = bus.subscribe<event_a>([&](const event_a&) { ++second_calls; });

    bus.emit<event_a>(0); // `first` removes `second` before the walk reaches it
    EXPECT_EQ(second_calls, 0);
    EXPECT_FALSE(static_cast<bool>(second));

    bus.emit<event_a>(0);
    EXPECT_EQ(second_calls, 0);
}

TEST(event_bus, manual_unsubscribe_during_dispatch_is_deferred_and_skips)
{
    core::event_bus bus;
    int second_calls = 0;
    std::uint64_t second_id = 0;
    auto first = bus.subscribe<event_a>([&](const event_a&) { bus.unsubscribe(second_id); });
    second_id = bus.subscribe<event_a>([&](const event_a&) { ++second_calls; }).release();

    bus.emit<event_a>(0);
    EXPECT_EQ(second_calls, 0);
    bus.emit<event_a>(0);
    EXPECT_EQ(second_calls, 0);
}

TEST(event_bus, listener_can_unsubscribe_itself_during_dispatch)
{
    core::event_bus bus;
    int calls = 0;
    core::subscription once;
    once = bus.subscribe<event_a>(
        [&](const event_a&)
        {
            ++calls;
            once.reset(); // one-shot: safe although this callable is executing
        });

    bus.emit<event_a>(0);
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(static_cast<bool>(once));
}

TEST(event_bus, listener_subscribed_and_dropped_within_one_dispatch_never_fires)
{
    core::event_bus bus;
    int calls = 0;
    auto sub = bus.subscribe<event_a>(
        [&](const event_a&)
        {
            auto transient = bus.subscribe<event_a>([&](const event_a&) { ++calls; });
            transient.reset(); // cancels the pending add
        });

    bus.emit<event_a>(0);
    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 0);
}

TEST(event_bus, nested_emit_inside_a_listener_is_delivered_in_place)
{
    core::event_bus bus;
    std::vector<int> order;
    auto sub_a = bus.subscribe<event_a>(
        [&](const event_a& e)
        {
            order.push_back(e.value);
            if (e.value == 0)
            {
                bus.emit<event_b>(1); // nested, different type
                bus.emit<event_a>(2); // nested, same type: re-walks this vector
            }
        });
    auto sub_b = bus.subscribe<event_b>([&](const event_b& e) { order.push_back(e.value); });

    bus.emit<event_a>(0);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0);
    EXPECT_EQ(order[1], 1);
    EXPECT_EQ(order[2], 2);
}

TEST(event_bus, changes_deferred_in_a_nested_dispatch_apply_when_the_outermost_returns)
{
    core::event_bus bus;
    int late_calls = 0;
    core::subscription late;
    auto outer = bus.subscribe<event_a>([&](const event_a&) { bus.emit<event_b>(0); });
    auto inner = bus.subscribe<event_b>(
        [&](const event_b&)
        {
            if (!late)
            {
                late = bus.subscribe<event_a>([&](const event_a&) { ++late_calls; });
            }
        });

    bus.emit<event_a>(0); // `late` is subscribed two levels deep
    EXPECT_EQ(late_calls, 0);
    bus.emit<event_a>(0);
    EXPECT_EQ(late_calls, 1);
}

TEST(event_bus, many_subscribes_during_dispatch_do_not_disturb_the_walk)
{
    // Stress the deferral: every listener of the first emit adds several
    // more of the same type, which would reallocate the vector under the
    // walk if the adds were applied immediately.
    core::event_bus bus;
    int calls = 0;
    std::vector<core::subscription> subs;
    subs.reserve(64);
    subs.push_back(bus.subscribe<event_a>(
        [&](const event_a&)
        {
            ++calls;
            if (subs.size() < 32)
            {
                for (int i = 0; i < 8; ++i)
                {
                    subs.push_back(bus.subscribe<event_a>([&](const event_a&) { ++calls; }));
                }
            }
        }));

    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(subs.size(), 9u);

    bus.emit<event_a>(0);
    EXPECT_EQ(calls, 1 + 9);
    EXPECT_EQ(subs.size(), 17u);
}

// --- buffered enqueue / flush -------------------------------------------------

TEST(event_bus, enqueued_events_are_not_delivered_until_flush)
{
    core::event_bus bus;
    int seen = 0;
    auto sub = bus.subscribe<event_a>([&](const event_a& e) { seen = e.value; });
    bus.enqueue<event_a>(99);
    EXPECT_EQ(seen, 0); // still buffered
    bus.flush();
    EXPECT_EQ(seen, 99);
}

TEST(event_bus, flush_delivers_in_fifo_order)
{
    core::event_bus bus;
    std::vector<int> order;
    auto sub = bus.subscribe<event_a>([&](const event_a& e) { order.push_back(e.value); });
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
    auto sub = bus.subscribe<event_a>([&](const event_a&) { ++calls; });
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
    auto sub = bus.subscribe<event_a>(
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
