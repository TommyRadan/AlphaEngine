// Unit tests for runtime::camera_component over the camera registry, headless:
// attaching registers the camera and parents it under the node (so the view
// follows the node's world pose with no per-frame copy), a disabled node's
// camera is skipped by the arbitration and returns on enable, a camera added
// to a disabled node starts disabled, two camera nodes arbitrate by priority
// and destroying the winner promotes the other, and removing the component
// (or destroying the node) detaches the camera.

#include <gtest/gtest.h>

#include <memory>

#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/camera/camera_registry.hpp>
#include <rendering_engine/camera/orthographic_camera.hpp>
#include <runtime/components/camera_component.hpp>
#include <runtime/scene_graph.hpp>

using core::math::mat4;
using core::math::vec3;
using core::math::vec4;
using rendering_engine::active_camera;
using rendering_engine::camera;
using rendering_engine::orthographic_camera;
using rendering_engine::registered_cameras;
using runtime::camera_component;
using runtime::context;
using runtime::node;

namespace
{
    constexpr float k_eps = 1e-4f;

    vec3 to_view(const mat4& view, const vec3& world)
    {
        const vec4 v = view * vec4{world, 1.0f};
        return vec3{v.x, v.y, v.z};
    }

    void expect_vec3_near(const vec3& actual, const vec3& expected, float eps = k_eps)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    // Every test starts and ends with an empty registry; the component's
    // camera detaches when the node or component goes.
    struct camera_component_fixture : ::testing::Test
    {
        void SetUp() override
        {
            ASSERT_TRUE(registered_cameras().empty());
        }
        void TearDown() override
        {
            EXPECT_TRUE(registered_cameras().empty());
        }
    };

    camera* add_camera(node& n, int priority = 0)
    {
        auto owned = std::make_unique<orthographic_camera>();
        owned->set_priority(priority);
        camera* cam = owned.get();
        n.add_component<camera_component>(camera_component{std::move(owned)});
        return cam;
    }
} // namespace

TEST_F(camera_component_fixture, attaching_registers_the_camera_and_destroying_the_node_detaches_it)
{
    context scene;
    camera* cam = nullptr;
    {
        node n;
        scene.root.add(n);
        cam = add_camera(n);
        EXPECT_TRUE(cam->is_attached());
        EXPECT_EQ(active_camera(), cam);
        EXPECT_EQ(cam->transform.get_parent(), &n.transform);
    }
    EXPECT_EQ(active_camera(), nullptr);
    EXPECT_TRUE(registered_cameras().empty());
}

TEST_F(camera_component_fixture, the_view_follows_the_node_world_pose_without_an_update)
{
    context scene;
    node rig;
    node n;
    scene.root.add(rig);
    rig.add(n);
    camera* cam = add_camera(n);

    rig.transform.set_position(vec3{10.0f, 0.0f, 0.0f});
    n.transform.set_position(vec3{0.0f, 5.0f, 0.0f});
    // No scene.update(): the camera's transform is parented under the node,
    // so the view is derived from the composed world pose on demand.
    const mat4 view = cam->get_view_matrix();
    expect_vec3_near(to_view(view, vec3{10.0f, 5.0f, 0.0f}), vec3{0.0f, 0.0f, 0.0f});
    expect_vec3_near(to_view(view, vec3{11.0f, 5.0f, 0.0f}), vec3{0.0f, 0.0f, -1.0f});

    // node::look_at orients the camera with it.
    n.look_at(vec3{10.0f, 9.0f, 0.0f});
    expect_vec3_near(to_view(cam->get_view_matrix(), vec3{10.0f, 9.0f, 0.0f}), vec3{0.0f, 0.0f, -4.0f});
}

TEST_F(camera_component_fixture, disabling_the_node_disables_the_camera_and_enabling_restores_it)
{
    context scene;
    node n;
    scene.root.add(n);
    camera* cam = add_camera(n);
    ASSERT_EQ(active_camera(), cam);

    n.set_active(false);
    EXPECT_TRUE(cam->is_attached());
    EXPECT_FALSE(cam->is_enabled());
    EXPECT_EQ(active_camera(), nullptr);

    n.set_active(true);
    EXPECT_TRUE(cam->is_enabled());
    EXPECT_EQ(active_camera(), cam);
}

TEST_F(camera_component_fixture, a_disabled_ancestor_disables_the_camera)
{
    context scene;
    node parent;
    node child;
    scene.root.add(parent);
    parent.add(child);
    camera* cam = add_camera(child);

    parent.set_active(false);
    EXPECT_EQ(active_camera(), nullptr);
    parent.set_active(true);
    EXPECT_EQ(active_camera(), cam);
}

TEST_F(camera_component_fixture, a_camera_added_to_a_disabled_node_starts_disabled)
{
    context scene;
    node n;
    scene.root.add(n);
    n.set_active(false);
    camera* cam = add_camera(n);
    EXPECT_TRUE(cam->is_attached());
    EXPECT_FALSE(cam->is_enabled());
    EXPECT_EQ(active_camera(), nullptr);
}

TEST_F(camera_component_fixture, two_camera_nodes_arbitrate_by_priority_and_promote_on_destroy)
{
    context scene;
    node player;
    scene.root.add(player);
    camera* player_cam = add_camera(player, 0);
    ASSERT_EQ(active_camera(), player_cam);

    {
        node cutscene;
        scene.root.add(cutscene);
        camera* cutscene_cam = add_camera(cutscene, 10);
        EXPECT_EQ(active_camera(), cutscene_cam);

        // Disabling the override hands the frame back...
        cutscene.set_active(false);
        EXPECT_EQ(active_camera(), player_cam);
        cutscene.set_active(true);
        EXPECT_EQ(active_camera(), cutscene_cam);
    }
    // ...and so does destroying it, with nothing re-attached by hand.
    EXPECT_EQ(active_camera(), player_cam);
}

TEST_F(camera_component_fixture, removing_the_component_detaches_and_unparents_the_camera)
{
    context scene;
    node n;
    scene.root.add(n);
    camera* cam = add_camera(n);
    ASSERT_TRUE(cam->is_attached());

    n.remove_component<camera_component>();
    EXPECT_EQ(active_camera(), nullptr);
    EXPECT_TRUE(registered_cameras().empty());
}

TEST_F(camera_component_fixture, an_empty_component_is_inert)
{
    context scene;
    node n;
    scene.root.add(n);
    n.add_component<camera_component>(camera_component{});
    EXPECT_EQ(n.get_component<camera_component>()->get(), nullptr);
    EXPECT_TRUE(registered_cameras().empty());
    n.set_active(false);
    n.set_active(true);
    n.remove_component<camera_component>();
}
