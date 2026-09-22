// Unit tests for gpu::backend::handle_pool, the generation-counted slot
// allocator both device backends mint their resource handles from:
// insert/lookup/remove, rejection of a removed handle once its slot is
// recycled, that clear() advances generations so a handle issued before a
// quit()/init() cycle never resolves again, and that a lookup pointer stays
// valid while the pool grows.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <rendering_engine/gpu/backend/handle_pool.hpp>

using rendering_engine::gpu::backend::handle_pool;

namespace
{
    struct record
    {
        int value{0};
    };
} // namespace

TEST(handle_pool, default_handle_never_resolves)
{
    handle_pool<record> pool;
    EXPECT_EQ(pool.lookup(0), nullptr);
    EXPECT_FALSE(pool.remove(0));

    pool.insert(record{1});
    EXPECT_EQ(pool.lookup(0), nullptr);
}

TEST(handle_pool, insert_then_lookup_returns_the_value)
{
    handle_pool<record> pool;
    const uint64_t h = pool.insert(record{42});
    ASSERT_NE(h, 0u);

    record* r = pool.lookup(h);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->value, 42);
}

TEST(handle_pool, distinct_inserts_get_distinct_handles)
{
    handle_pool<record> pool;
    const uint64_t a = pool.insert(record{1});
    const uint64_t b = pool.insert(record{2});
    EXPECT_NE(a, b);
    EXPECT_EQ(pool.lookup(a)->value, 1);
    EXPECT_EQ(pool.lookup(b)->value, 2);
}

TEST(handle_pool, removed_handle_is_rejected_after_slot_recycling)
{
    handle_pool<record> pool;
    const uint64_t first = pool.insert(record{1});
    EXPECT_TRUE(pool.remove(first));
    EXPECT_EQ(pool.lookup(first), nullptr);
    EXPECT_FALSE(pool.remove(first));

    // The freed slot is reused, but under a new generation.
    const uint64_t second = pool.insert(record{2});
    EXPECT_NE(second, first);
    EXPECT_EQ(pool.lookup(first), nullptr);
    ASSERT_NE(pool.lookup(second), nullptr);
    EXPECT_EQ(pool.lookup(second)->value, 2);
}

TEST(handle_pool, generation_survives_clear)
{
    handle_pool<record> pool;
    const uint64_t before = pool.insert(record{1});
    pool.clear();

    // The next lifetime takes the same slot index, so without a
    // generation bump this would mint the identical id.
    const uint64_t after = pool.insert(record{2});
    EXPECT_NE(after, before);
    EXPECT_EQ(pool.lookup(before), nullptr);
    ASSERT_NE(pool.lookup(after), nullptr);
    EXPECT_EQ(pool.lookup(after)->value, 2);
}

TEST(handle_pool, stale_handles_are_rejected_after_clear_and_refill)
{
    handle_pool<record> pool;
    std::vector<uint64_t> old_handles;
    for (int i = 0; i < 8; ++i)
    {
        old_handles.push_back(pool.insert(record{i}));
    }
    // Free a couple so the free list is non-empty when the pool is
    // cleared; clear() must drop the free list too.
    EXPECT_TRUE(pool.remove(old_handles[2]));
    EXPECT_TRUE(pool.remove(old_handles[5]));

    pool.clear();

    std::vector<uint64_t> new_handles;
    for (int i = 0; i < 8; ++i)
    {
        new_handles.push_back(pool.insert(record{100 + i}));
    }

    for (const uint64_t old : old_handles)
    {
        EXPECT_EQ(pool.lookup(old), nullptr);
        EXPECT_FALSE(pool.remove(old));
    }
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_NE(pool.lookup(new_handles[i]), nullptr);
        EXPECT_EQ(pool.lookup(new_handles[i])->value, 100 + i);
    }
}

TEST(handle_pool, repeated_clears_keep_advancing_generations)
{
    handle_pool<record> pool;
    const uint64_t first = pool.insert(record{1});
    pool.clear();
    const uint64_t second = pool.insert(record{2});
    pool.clear();
    const uint64_t third = pool.insert(record{3});

    EXPECT_NE(first, second);
    EXPECT_NE(second, third);
    EXPECT_NE(first, third);
    EXPECT_EQ(pool.lookup(first), nullptr);
    EXPECT_EQ(pool.lookup(second), nullptr);
    ASSERT_NE(pool.lookup(third), nullptr);
    EXPECT_EQ(pool.lookup(third)->value, 3);
}

TEST(handle_pool, lookup_pointer_is_stable_across_growth)
{
    handle_pool<record> pool;
    const uint64_t h = pool.insert(record{42});
    record* before = pool.lookup(h);
    ASSERT_NE(before, nullptr);

    // Grow well past any initial capacity a vector-backed slab would
    // have reserved.
    std::vector<uint64_t> handles;
    for (int i = 0; i < 4096; ++i)
    {
        handles.push_back(pool.insert(record{i}));
    }

    EXPECT_EQ(pool.lookup(h), before);
    EXPECT_EQ(before->value, 42);
    EXPECT_EQ(pool.lookup(handles.back())->value, 4095);
}

TEST(handle_pool, for_each_visits_only_live_entries)
{
    handle_pool<record> pool;
    const uint64_t a = pool.insert(record{1});
    pool.insert(record{2});
    const uint64_t c = pool.insert(record{3});
    EXPECT_TRUE(pool.remove(a));
    EXPECT_TRUE(pool.remove(c));

    int sum = 0;
    int count = 0;
    pool.for_each(
        [&](record& r)
        {
            sum += r.value;
            ++count;
        });
    EXPECT_EQ(count, 1);
    EXPECT_EQ(sum, 2);

    pool.clear();
    count = 0;
    pool.for_each([&](record&) { ++count; });
    EXPECT_EQ(count, 0);
}
