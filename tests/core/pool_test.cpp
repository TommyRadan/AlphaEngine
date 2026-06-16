// Unit tests for core::pool: insert/get/erase, generation-based handle
// invalidation, and free-list slot recycling.

#include <gtest/gtest.h>

#include <core/pool.hpp>

using core::pool;
using core::pool_handle;

TEST(pool, default_handle_is_invalid)
{
    pool_handle<int> h;
    EXPECT_FALSE(h.valid());
}

TEST(pool, insert_then_get_returns_value)
{
    pool<int> p;
    auto h = p.insert(42);
    EXPECT_TRUE(h.valid());
    EXPECT_TRUE(p.contains(h));
    ASSERT_NE(p.get(h), nullptr);
    EXPECT_EQ(*p.get(h), 42);
    EXPECT_EQ(p.size(), 1u);
    EXPECT_FALSE(p.empty());
}

TEST(pool, default_constructed_handle_never_names_a_live_slot)
{
    pool<int> p;
    p.insert(7);
    pool_handle<int> empty;
    EXPECT_FALSE(p.contains(empty));
    EXPECT_EQ(p.get(empty), nullptr);
}

TEST(pool, erase_invalidates_the_handle)
{
    pool<int> p;
    auto h = p.insert(99);
    p.erase(h);
    EXPECT_FALSE(p.contains(h));
    EXPECT_EQ(p.get(h), nullptr);
    EXPECT_EQ(p.size(), 0u);
    EXPECT_TRUE(p.empty());
}

TEST(pool, double_erase_is_a_noop)
{
    pool<int> p;
    auto h = p.insert(1);
    p.erase(h);
    p.erase(h); // must not corrupt the free list or underflow size
    EXPECT_EQ(p.size(), 0u);
}

TEST(pool, recycled_slot_makes_the_old_handle_stale)
{
    pool<int> p;
    auto first = p.insert(10);
    p.erase(first);
    // The next insert reuses the same index with a bumped generation.
    auto second = p.insert(20);
    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.generation, first.generation);
    // The stale handle must not alias the recycled slot.
    EXPECT_FALSE(p.contains(first));
    EXPECT_EQ(p.get(first), nullptr);
    ASSERT_NE(p.get(second), nullptr);
    EXPECT_EQ(*p.get(second), 20);
}

TEST(pool, distinct_live_handles_address_distinct_values)
{
    pool<int> p;
    auto a = p.insert(1);
    auto b = p.insert(2);
    auto c = p.insert(3);
    EXPECT_EQ(*p.get(a), 1);
    EXPECT_EQ(*p.get(b), 2);
    EXPECT_EQ(*p.get(c), 3);
    EXPECT_EQ(p.size(), 3u);

    p.erase(b);
    EXPECT_EQ(*p.get(a), 1);
    EXPECT_EQ(*p.get(c), 3);
    EXPECT_FALSE(p.contains(b));
}

TEST(pool, mutation_through_get_pointer_is_visible)
{
    pool<int> p;
    auto h = p.insert(5);
    *p.get(h) = 17;
    EXPECT_EQ(*p.get(h), 17);
}
