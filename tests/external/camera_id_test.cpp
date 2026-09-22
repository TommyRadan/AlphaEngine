// Unit tests for the external camera facade's generational camera_id: the
// slot-index / generation packing declared in external/api/camera.hpp and,
// over the core::pool the facade allocates from, that a destroyed camera's
// id never resolves again once its slot has been recycled. The facade's
// translation unit links the renderer, so only the header is exercised here.

#include <gtest/gtest.h>

#include <core/pool.hpp>
#include <external/api/camera.hpp>

namespace
{
    struct camera_id_tag
    {
    };

    using test_pool = core::pool<int, camera_id_tag>;
    using test_handle = test_pool::handle;

    camera_id to_camera_id(test_handle handle)
    {
        return make_camera_id(handle.index, handle.generation);
    }

    test_handle to_handle(camera_id id)
    {
        return test_handle{camera_id_index(id), camera_id_generation(id)};
    }
} // namespace

TEST(camera_id, invalid_id_is_zero_and_unpacks_to_an_invalid_handle)
{
    EXPECT_EQ(invalid_camera_id, 0u);
    EXPECT_EQ(camera_id_index(invalid_camera_id), 0u);
    EXPECT_EQ(camera_id_generation(invalid_camera_id), 0u);
    EXPECT_FALSE(to_handle(invalid_camera_id).valid());
}

TEST(camera_id, packing_round_trips_index_and_generation)
{
    const camera_id id = make_camera_id(0xdeadbeefu, 0x0badf00du);
    EXPECT_EQ(camera_id_index(id), 0xdeadbeefu);
    EXPECT_EQ(camera_id_generation(id), 0x0badf00du);
    EXPECT_NE(id, invalid_camera_id);
}

TEST(camera_id, first_slot_of_first_generation_is_not_the_invalid_id)
{
    // Live pool handles start at generation 1, so even slot 0 packs to a non-zero id.
    EXPECT_NE(make_camera_id(0u, 1u), invalid_camera_id);
}

TEST(camera_id, pool_handle_survives_the_round_trip)
{
    test_pool cameras;
    const test_handle handle = cameras.insert(7);
    const camera_id id = to_camera_id(handle);
    EXPECT_NE(id, invalid_camera_id);
    EXPECT_TRUE(to_handle(id) == handle);
    ASSERT_NE(cameras.get(to_handle(id)), nullptr);
    EXPECT_EQ(*cameras.get(to_handle(id)), 7);
}

TEST(camera_id, destroyed_id_never_resolves_again)
{
    test_pool cameras;
    const camera_id first = to_camera_id(cameras.insert(1));
    cameras.erase(to_handle(first));

    // The slot is recycled with a bumped generation, so the new id differs...
    const camera_id second = to_camera_id(cameras.insert(2));
    EXPECT_EQ(camera_id_index(second), camera_id_index(first));
    EXPECT_NE(camera_id_generation(second), camera_id_generation(first));
    EXPECT_NE(second, first);

    // ...and the stale id does not alias the camera now occupying the slot.
    EXPECT_EQ(cameras.get(to_handle(first)), nullptr);
    ASSERT_NE(cameras.get(to_handle(second)), nullptr);
    EXPECT_EQ(*cameras.get(to_handle(second)), 2);
}

TEST(camera_id, invalid_id_never_names_a_live_slot)
{
    test_pool cameras;
    cameras.insert(3);
    EXPECT_EQ(cameras.get(to_handle(invalid_camera_id)), nullptr);
}
