// Unit tests for core/hash.hpp: the 64-bit FNV-1a digest against the
// published reference vectors, seeded chaining, and the incremental hasher
// agreeing with the one-shot function over the concatenated input.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <string_view>

#include <core/hash.hpp>

TEST(fnv1a_64, reference_vectors)
{
    EXPECT_EQ(core::fnv1a_64(""), 0xcbf29ce484222325ULL);
    EXPECT_EQ(core::fnv1a_64("a"), 0xaf63dc4c8601ec8cULL);
    EXPECT_EQ(core::fnv1a_64("foobar"), 0x85944171f73967e8ULL);
}

TEST(fnv1a_64, is_constexpr)
{
    constexpr uint64_t digest = core::fnv1a_64("foobar");
    static_assert(digest == 0x85944171f73967e8ULL);
    EXPECT_EQ(digest, 0x85944171f73967e8ULL);
}

TEST(fnv1a_64, seeded_chaining_hashes_the_concatenation)
{
    const uint64_t whole = core::fnv1a_64("foobar");
    const uint64_t chained = core::fnv1a_64("bar", core::fnv1a_64("foo"));
    EXPECT_EQ(chained, whole);
    EXPECT_NE(core::fnv1a_64("foo"), whole);
}

TEST(fnv1a_64, hasher_matches_one_shot_and_orders_inputs)
{
    core::fnv1a_64_hasher hasher;
    hasher.mix("foo");
    hasher.mix(std::string_view{"bar"});
    EXPECT_EQ(hasher.value(), core::fnv1a_64("foobar"));

    core::fnv1a_64_hasher reversed;
    reversed.mix("bar");
    reversed.mix("foo");
    EXPECT_NE(reversed.value(), hasher.value());

    const std::string bytes = "foobar";
    core::fnv1a_64_hasher raw;
    raw.mix(bytes.data(), bytes.size());
    EXPECT_EQ(raw.value(), hasher.value());
}

TEST(fnv1a_64, mix_value_folds_the_object_bytes)
{
    core::fnv1a_64_hasher with_one;
    with_one.mix_value(1);
    core::fnv1a_64_hasher with_two;
    with_two.mix_value(2);
    EXPECT_NE(with_one.value(), with_two.value());

    const int one = 1;
    core::fnv1a_64_hasher raw;
    raw.mix(&one, sizeof(one));
    EXPECT_EQ(raw.value(), with_one.value());

    core::fnv1a_64_hasher empty;
    EXPECT_EQ(empty.value(), core::fnv1a_64_offset_basis);
}
