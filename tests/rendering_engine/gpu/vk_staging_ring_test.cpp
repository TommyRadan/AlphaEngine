/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// Device-free tests for the Vulkan backend's staging-ring bookkeeping
// (rendering_engine/gpu/backend/vulkan/vk_staging_ring.hpp): reservation
// offsets and alignment, the wrap past the end of the buffer, sealing
// batches and reclaiming their bytes in fence order. The class is plain
// integer arithmetic; the device binds it to the real buffer and fences.

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <vector>

#include <rendering_engine/gpu/backend/vulkan/vk_staging_ring.hpp>

namespace
{
    using rendering_engine::gpu::backend::vulkan::staging_ring;

    // Reserve and require success; the test then reads the offset.
    uint64_t must_allocate(staging_ring& ring, uint64_t size, uint64_t alignment)
    {
        const std::optional<uint64_t> offset = ring.allocate(size, alignment);
        EXPECT_TRUE(offset.has_value()) << "size " << size << " alignment " << alignment;
        return offset.value_or(~0ull);
    }
} // namespace

// ---- reservation ----------------------------------------------------------

TEST(vk_staging_ring, a_fresh_ring_hands_out_contiguous_offsets_from_zero)
{
    staging_ring ring{1024};
    EXPECT_EQ(ring.capacity(), 1024u);
    EXPECT_EQ(ring.used(), 0u);
    EXPECT_EQ(must_allocate(ring, 100, 1), 0u);
    EXPECT_EQ(must_allocate(ring, 100, 1), 100u);
    EXPECT_EQ(must_allocate(ring, 24, 1), 200u);
    EXPECT_EQ(ring.used(), 224u);
    EXPECT_EQ(ring.pending(), 224u);
}

TEST(vk_staging_ring, offsets_are_aligned_and_the_padding_counts_as_used)
{
    staging_ring ring{1024};
    EXPECT_EQ(must_allocate(ring, 1, 16), 0u);
    // The next reservation skips the 15 bytes up to the alignment.
    EXPECT_EQ(must_allocate(ring, 10, 16), 16u);
    EXPECT_EQ(must_allocate(ring, 4, 64), 64u);
    EXPECT_EQ(ring.used(), 68u);
    // Alignment 0 means unaligned.
    EXPECT_EQ(must_allocate(ring, 1, 0), 68u);
    EXPECT_EQ(must_allocate(ring, 1, 1), 69u);
}

TEST(vk_staging_ring, a_reservation_of_zero_bytes_or_more_than_the_capacity_is_refused)
{
    staging_ring ring{256};
    EXPECT_FALSE(ring.allocate(0, 16).has_value());
    EXPECT_FALSE(ring.allocate(257, 16).has_value());
    EXPECT_EQ(ring.used(), 0u);
    // Exactly the capacity fits an empty ring.
    EXPECT_EQ(must_allocate(ring, 256, 16), 0u);
    EXPECT_EQ(ring.used(), 256u);
}

TEST(vk_staging_ring, a_default_constructed_ring_refuses_everything)
{
    staging_ring ring;
    EXPECT_EQ(ring.capacity(), 0u);
    EXPECT_FALSE(ring.allocate(1, 1).has_value());
}

// ---- the ring fills up ----------------------------------------------------

TEST(vk_staging_ring, the_ring_refuses_once_the_open_batch_holds_everything)
{
    staging_ring ring{256};
    EXPECT_EQ(must_allocate(ring, 200, 1), 0u);
    // 56 bytes remain; 100 do not fit and the state is untouched.
    EXPECT_FALSE(ring.allocate(100, 1).has_value());
    EXPECT_EQ(ring.used(), 200u);
    EXPECT_EQ(must_allocate(ring, 56, 1), 200u);
    EXPECT_FALSE(ring.allocate(1, 1).has_value());
    EXPECT_EQ(ring.used(), 256u);
}

TEST(vk_staging_ring, sealing_records_the_batch_and_moves_the_open_cursor)
{
    staging_ring ring{1024};
    must_allocate(ring, 100, 1);
    must_allocate(ring, 100, 1);
    EXPECT_EQ(ring.pending(), 200u);
    EXPECT_EQ(ring.sealed_count(), 0u);
    EXPECT_FALSE(ring.oldest_sealed().has_value());

    EXPECT_TRUE(ring.seal(7));
    EXPECT_EQ(ring.pending(), 0u);
    EXPECT_EQ(ring.used(), 200u);
    EXPECT_EQ(ring.sealed_count(), 1u);
    EXPECT_EQ(ring.oldest_sealed(), std::optional<uint64_t>{7});

    must_allocate(ring, 50, 1);
    EXPECT_EQ(ring.pending(), 50u);
    EXPECT_TRUE(ring.seal(8));
    EXPECT_EQ(ring.sealed_count(), 2u);
    EXPECT_EQ(ring.oldest_sealed(), std::optional<uint64_t>{7});
}

TEST(vk_staging_ring, sealing_an_empty_batch_records_nothing)
{
    staging_ring ring{1024};
    EXPECT_FALSE(ring.seal(1));
    EXPECT_EQ(ring.sealed_count(), 0u);
    must_allocate(ring, 8, 1);
    EXPECT_TRUE(ring.seal(2));
    // Nothing reserved since the last seal: refused again.
    EXPECT_FALSE(ring.seal(3));
    EXPECT_EQ(ring.sealed_count(), 1u);
}

// ---- reclaim --------------------------------------------------------------

TEST(vk_staging_ring, retiring_a_batch_releases_exactly_its_bytes)
{
    staging_ring ring{256};
    must_allocate(ring, 100, 1);
    ring.seal(1);
    must_allocate(ring, 100, 1);
    ring.seal(2);
    EXPECT_EQ(ring.used(), 200u);
    EXPECT_FALSE(ring.allocate(100, 1).has_value());

    ring.retire(1);
    EXPECT_EQ(ring.used(), 100u);
    EXPECT_EQ(ring.sealed_count(), 1u);
    EXPECT_EQ(ring.oldest_sealed(), std::optional<uint64_t>{2});

    ring.retire(2);
    EXPECT_EQ(ring.used(), 0u);
    EXPECT_EQ(ring.sealed_count(), 0u);
}

TEST(vk_staging_ring, retiring_a_later_batch_releases_the_earlier_ones_too)
{
    // A fence that signals for batch 3 proves batches 1 and 2 (submitted
    // ahead of it on the same queue) complete.
    staging_ring ring{1024};
    for (uint64_t id = 1; id <= 3; ++id)
    {
        must_allocate(ring, 100, 1);
        ring.seal(id);
    }
    must_allocate(ring, 10, 1); // open batch
    ring.retire(3);
    EXPECT_EQ(ring.sealed_count(), 0u);
    EXPECT_EQ(ring.used(), 10u);
    EXPECT_EQ(ring.pending(), 10u);
}

TEST(vk_staging_ring, retiring_an_unknown_or_older_id_is_a_no_op)
{
    staging_ring ring{1024};
    must_allocate(ring, 100, 1);
    ring.seal(5);
    ring.retire(4);
    EXPECT_EQ(ring.used(), 100u);
    EXPECT_EQ(ring.sealed_count(), 1u);
    ring.retire(5);
    ring.retire(5);
    ring.retire(9);
    EXPECT_EQ(ring.used(), 0u);
    EXPECT_EQ(ring.sealed_count(), 0u);
}

TEST(vk_staging_ring, retiring_never_releases_the_open_batch)
{
    staging_ring ring{256};
    must_allocate(ring, 100, 1);
    ring.seal(1);
    must_allocate(ring, 100, 1);
    ring.retire(1000);
    EXPECT_EQ(ring.used(), 100u);
    EXPECT_EQ(ring.pending(), 100u);
    // The released bytes are usable (the ring wraps into them), the
    // open batch's are not.
    EXPECT_EQ(must_allocate(ring, 56, 1), 200u);
    EXPECT_EQ(must_allocate(ring, 100, 1), 0u);
    EXPECT_EQ(ring.used(), 256u);
    EXPECT_FALSE(ring.allocate(1, 1).has_value());
}

// ---- wrap -----------------------------------------------------------------

TEST(vk_staging_ring, a_reservation_that_does_not_fit_before_the_end_wraps_to_zero)
{
    staging_ring ring{256};
    must_allocate(ring, 200, 1);
    ring.seal(1);
    ring.retire(1);
    // Head is at 200 with 56 bytes to the end; 100 bytes go to offset
    // 0 of the next lap and the 56 skipped bytes count as used.
    EXPECT_EQ(must_allocate(ring, 100, 1), 0u);
    EXPECT_EQ(ring.used(), 156u);
    EXPECT_EQ(must_allocate(ring, 50, 1), 100u);
}

TEST(vk_staging_ring, a_wrapped_reservation_still_respects_the_tail)
{
    staging_ring ring{256};
    // Batch 1 holds [0, 100), still in flight.
    must_allocate(ring, 100, 1);
    ring.seal(1);
    // The open batch holds [100, 220).
    must_allocate(ring, 120, 1);
    // 36 bytes remain before the end; 50 would wrap to offset 0, which
    // batch 1 still owns, so it is refused.
    EXPECT_FALSE(ring.allocate(50, 1).has_value());
    EXPECT_EQ(ring.used(), 220u);
    ring.retire(1);
    // With batch 1 gone the wrap lands at 0 and the tail of the lap is
    // skipped: [220, 256) wasted, [0, 50) reserved.
    EXPECT_EQ(must_allocate(ring, 50, 1), 0u);
    EXPECT_EQ(ring.used(), 256u - 100u + 50u);
}

TEST(vk_staging_ring, skipped_lap_tails_are_released_with_the_batch_that_skipped_them)
{
    staging_ring ring{256};
    must_allocate(ring, 200, 1);
    ring.seal(1);
    ring.retire(1);
    must_allocate(ring, 100, 1); // wraps; 56 bytes skipped
    ring.seal(2);
    EXPECT_EQ(ring.used(), 156u);
    ring.retire(2);
    EXPECT_EQ(ring.used(), 0u);
    // Head is at 100 of lap two; the ring keeps going.
    EXPECT_EQ(must_allocate(ring, 156, 1), 100u);
    EXPECT_EQ(ring.used(), 156u);
}

TEST(vk_staging_ring, offsets_stay_aligned_across_laps)
{
    // The device sizes the ring as a multiple of the alignment; every
    // offset on every lap is then a multiple of it.
    staging_ring ring{4096};
    std::vector<uint64_t> offsets;
    uint64_t id = 1;
    for (int i = 0; i < 200; ++i)
    {
        std::optional<uint64_t> offset = ring.allocate(300, 64);
        if (!offset.has_value())
        {
            ring.seal(id);
            ring.retire(id);
            ++id;
            offset = ring.allocate(300, 64);
        }
        ASSERT_TRUE(offset.has_value());
        EXPECT_EQ(*offset % 64, 0u);
        EXPECT_LE(*offset + 300, 4096u);
        offsets.push_back(*offset);
    }
    // Twelve reservations fit on the first lap (the twelfth ends at
    // 3820); the thirteenth wraps.
    ASSERT_GT(offsets.size(), 12u);
    EXPECT_EQ(offsets[11], 3520u);
    EXPECT_EQ(offsets[12], 0u);
}

TEST(vk_staging_ring, an_alignment_the_capacity_is_not_a_multiple_of_wraps_to_offset_zero)
{
    staging_ring ring{100};
    EXPECT_EQ(must_allocate(ring, 60, 16), 0u);
    ring.seal(1);
    ring.retire(1);
    // Aligned up to 64 and 40 bytes would end at 104 > 100: next lap.
    EXPECT_EQ(must_allocate(ring, 40, 16), 0u);
    EXPECT_EQ(ring.used(), 80u);
}

// ---- the device's flush-and-wait loop --------------------------------------

TEST(vk_staging_ring, a_full_ring_drains_batch_by_batch)
{
    // Mirrors vk_device::stage_upload: when a reservation fails, the open
    // batch is sealed (submitted) and the oldest batch waited and retired,
    // then the reservation is retried. Each round makes progress and the
    // upload eventually lands.
    staging_ring ring{1024};
    uint64_t next_id = 1;
    for (int upload = 0; upload < 50; ++upload)
    {
        int rounds = 0;
        while (!ring.allocate(300, 16).has_value())
        {
            ring.seal(next_id++);
            const std::optional<uint64_t> oldest = ring.oldest_sealed();
            ASSERT_TRUE(oldest.has_value()) << "upload " << upload;
            ring.retire(*oldest);
            ASSERT_LT(++rounds, 8) << "upload " << upload;
        }
        EXPECT_LE(ring.used(), 1024u);
    }
}
