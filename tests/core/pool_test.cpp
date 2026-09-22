// Unit tests for core::pool: insert/get/erase, generation-based handle
// invalidation (including the wrap past the invalid generation 0), free-list
// slot recycling, in-place construction and destruction of values that are
// neither default-constructible nor copyable, and iteration over live slots.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <core/pool.hpp>

using core::pool;
using core::pool_handle;

// Friend declared in pool.hpp for exactly this: reach the private slot table so
// the wrap test can start a slot at the top of the generation counter instead
// of recycling it four billion times.
struct core::pool_test_access
{
    template<typename T, typename Tag>
    static void set_generation(pool<T, Tag>& p, uint32_t index, uint32_t generation)
    {
        p.m_slots[index].generation = generation;
    }
};

namespace
{
    // Neither default-constructible nor copyable, and it reports its own
    // destruction: the pool must construct it in place on insert and destroy
    // it in place on erase, exactly once.
    struct tracked
    {
        tracked(int v, int* destroyed) : value{v}, destroyed_counter{destroyed}
        {
        }
        tracked(tracked&& other) noexcept : value{other.value}, destroyed_counter{other.destroyed_counter}
        {
            other.destroyed_counter = nullptr; // a moved-from husk is not a destruction
        }
        tracked(const tracked&) = delete;
        tracked& operator=(const tracked&) = delete;
        tracked& operator=(tracked&&) = delete;
        ~tracked()
        {
            if (destroyed_counter != nullptr)
            {
                ++*destroyed_counter;
            }
        }

        int value;
        int* destroyed_counter;
    };
} // namespace

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

TEST(pool, generation_skips_zero_when_it_wraps)
{
    pool<int> p;
    auto first = p.insert(1);
    // Put the slot one recycle away from wrapping, as if it had been reused
    // 2^32 - 2 times. The handle we hold no longer matches; build the one the
    // pool would have handed out at that generation.
    core::pool_test_access::set_generation(p, first.index, UINT32_MAX);
    pool_handle<int> top{first.index, UINT32_MAX};
    ASSERT_TRUE(p.contains(top));

    p.erase(top);
    auto recycled = p.insert(2);

    // The wrap lands on 1, never on the invalid generation 0 — which would
    // have made the slot unreachable and lost it from the free list for good.
    EXPECT_EQ(recycled.index, first.index);
    EXPECT_EQ(recycled.generation, 1u);
    EXPECT_TRUE(recycled.valid());
    EXPECT_TRUE(p.contains(recycled));
    ASSERT_NE(p.get(recycled), nullptr);
    EXPECT_EQ(*p.get(recycled), 2);
    EXPECT_FALSE(p.contains(top));
    EXPECT_EQ(p.size(), 1u);
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

TEST(pool, holds_move_only_values)
{
    pool<std::unique_ptr<int>> p;
    auto h = p.insert(std::make_unique<int>(7));
    ASSERT_NE(p.get(h), nullptr);
    ASSERT_NE(p.get(h)->get(), nullptr);
    EXPECT_EQ(**p.get(h), 7);
    p.erase(h);
    EXPECT_FALSE(p.contains(h));
    EXPECT_TRUE(p.empty());
}

TEST(pool, holds_values_that_are_not_default_constructible)
{
    int destroyed = 0;
    pool<tracked> p;
    auto h = p.insert(tracked{3, &destroyed});
    ASSERT_NE(p.get(h), nullptr);
    EXPECT_EQ(p.get(h)->value, 3);
    EXPECT_EQ(destroyed, 0); // the moved-from temporary does not count
}

TEST(pool, erase_destroys_the_value_in_place)
{
    int destroyed = 0;
    pool<tracked> p;
    auto h = p.insert(tracked{1, &destroyed});
    EXPECT_EQ(destroyed, 0);

    p.erase(h);
    EXPECT_EQ(destroyed, 1) << "erase must run the destructor immediately";

    // Reusing the slot constructs a new value; it destroys nothing.
    auto again = p.insert(tracked{2, &destroyed});
    EXPECT_EQ(again.index, h.index);
    EXPECT_EQ(destroyed, 1);

    p.erase(h); // stale: must not touch the new occupant
    EXPECT_EQ(destroyed, 1);
    ASSERT_NE(p.get(again), nullptr);
    EXPECT_EQ(p.get(again)->value, 2);
}

TEST(pool, destructor_destroys_every_live_value_once)
{
    int destroyed = 0;
    {
        pool<tracked> p;
        p.insert(tracked{1, &destroyed});
        auto b = p.insert(tracked{2, &destroyed});
        p.insert(tracked{3, &destroyed});
        p.erase(b);
        EXPECT_EQ(destroyed, 1);
    }
    EXPECT_EQ(destroyed, 3);
}

TEST(pool, iteration_visits_only_live_values_in_slot_order)
{
    pool<int> p;
    p.insert(10);
    auto b = p.insert(20);
    p.insert(30);
    p.erase(b);

    std::vector<int> seen;
    for (int value : p)
    {
        seen.push_back(value);
    }
    EXPECT_EQ(seen, (std::vector<int>{10, 30}));

    int sum = 0;
    p.for_each([&](int value) { sum += value; });
    EXPECT_EQ(sum, 40);

    // The freed slot is reused first, so the new value lands between the others.
    p.insert(25);
    seen.clear();
    for (int value : p)
    {
        seen.push_back(value);
    }
    EXPECT_EQ(seen, (std::vector<int>{10, 25, 30}));
}

TEST(pool, const_iteration_and_for_each_see_the_same_values)
{
    pool<int> p;
    p.insert(1);
    p.insert(2);
    auto h = p.insert(3);
    p.erase(h);
    const pool<int>& view = p;

    std::vector<int> iterated;
    for (const int& value : view)
    {
        iterated.push_back(value);
    }
    std::vector<int> visited;
    view.for_each([&](const int& value) { visited.push_back(value); });

    EXPECT_EQ(iterated, (std::vector<int>{1, 2}));
    EXPECT_EQ(visited, iterated);
}

TEST(pool, iteration_over_an_empty_pool_yields_nothing)
{
    pool<int> p;
    EXPECT_TRUE(p.begin() == p.end());

    auto h = p.insert(1);
    p.erase(h);
    // Every slot free (but allocated): still nothing to visit.
    EXPECT_TRUE(p.begin() == p.end());
    int calls = 0;
    p.for_each([&](int) { ++calls; });
    EXPECT_EQ(calls, 0);
}

TEST(pool, iterator_handle_names_the_slot_it_points_at)
{
    pool<int> p;
    auto a = p.insert(1);
    auto b = p.insert(2);
    p.erase(a);

    auto it = p.begin();
    ASSERT_TRUE(it != p.end());
    EXPECT_TRUE(it.handle() == b);
    EXPECT_EQ(p.get(it.handle()), &*it);
    ++it;
    EXPECT_TRUE(it == p.end());
}

TEST(pool, mutation_through_iteration_is_visible)
{
    pool<int> p;
    auto h = p.insert(5);
    for (int& value : p)
    {
        value *= 2;
    }
    EXPECT_EQ(*p.get(h), 10);
    p.for_each([](int& value) { value += 1; });
    EXPECT_EQ(*p.get(h), 11);
}
