// Unit tests for runtime::light_component over the renderer's light registry:
// a disabled node takes its light out of registered_lights() and a re-enabled
// one puts it back, on_update tracks the node's world pose (a point light's
// position; a directional light's direction along the node's world +X, kept
// rather than NaN when a zero-scale node has none), and the component's
// destruction unregisters the light. The registry is a plain vector of
// back-pointers, so this runs device-free.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include <core/math/vec3.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <runtime/components/light_component.hpp>
#include <runtime/scene.hpp>

using core::math::vec3;
using rendering_engine::light;
using rendering_engine::point_light;
using rendering_engine::registered_lights;
using runtime::light_component;
using runtime::node;

namespace
{
    constexpr float k_eps = 1e-4f;

    bool is_registered(const light* l)
    {
        const auto& lights = registered_lights();
        return std::find(lights.begin(), lights.end(), l) != lights.end();
    }
} // namespace

TEST(light, set_enabled_false_removes_it_from_the_registry_and_true_restores_it)
{
    point_light l;
    EXPECT_TRUE(l.is_enabled());
    EXPECT_TRUE(is_registered(&l));

    l.set_enabled(false);
    EXPECT_FALSE(l.is_enabled());
    EXPECT_FALSE(is_registered(&l));

    l.set_enabled(true);
    EXPECT_TRUE(l.is_enabled());
    EXPECT_TRUE(is_registered(&l));
}

TEST(light, a_disabled_light_is_gone_from_the_registry_once_destroyed)
{
    const light* address = nullptr;
    {
        point_light l;
        address = &l;
        l.set_enabled(false);
    }
    EXPECT_FALSE(is_registered(address));
}

TEST(light_component, disabling_the_node_unregisters_the_light_and_enabling_restores_it)
{
    runtime::scene scene;
    node n;
    scene.root.add(n);
    auto owned = std::make_unique<point_light>();
    const light* l = owned.get();
    n.add_component<light_component>(light_component{std::move(owned)});
    ASSERT_TRUE(is_registered(l));

    n.set_active(false);
    EXPECT_FALSE(is_registered(l));
    EXPECT_FALSE(l->is_enabled());

    n.set_active(true);
    EXPECT_TRUE(is_registered(l));
    EXPECT_TRUE(l->is_enabled());
}

TEST(light_component, a_disabled_ancestor_disables_the_light)
{
    runtime::scene scene;
    node parent;
    node child;
    scene.root.add(parent);
    parent.add(child);
    auto owned = std::make_unique<point_light>();
    const light* l = owned.get();
    child.add_component<light_component>(light_component{std::move(owned)});

    parent.set_active(false);
    EXPECT_FALSE(is_registered(l));
    parent.set_active(true);
    EXPECT_TRUE(is_registered(l));
}

TEST(light_component, a_light_added_to_a_disabled_node_starts_disabled)
{
    runtime::scene scene;
    node n;
    scene.root.add(n);
    n.set_active(false);
    auto owned = std::make_unique<point_light>();
    const light* l = owned.get();
    n.add_component<light_component>(light_component{std::move(owned)});
    EXPECT_FALSE(is_registered(l));
}

TEST(light_component, update_moves_a_point_light_to_the_node_world_position)
{
    runtime::scene scene;
    node parent;
    node n;
    scene.root.add(parent);
    parent.add(n);
    parent.transform.set_position(vec3{10.0f, 0.0f, 0.0f});
    n.transform.set_position(vec3{0.0f, 5.0f, 0.0f});
    auto owned = std::make_unique<point_light>();
    const point_light* l = owned.get();
    n.add_component<light_component>(light_component{std::move(owned)});

    scene.update();
    EXPECT_NEAR(l->position.x, 10.0f, k_eps);
    EXPECT_NEAR(l->position.y, 5.0f, k_eps);
    EXPECT_NEAR(l->position.z, 0.0f, k_eps);
}

TEST(light_component, update_aims_a_directional_light_along_the_node_world_forward)
{
    runtime::scene scene;
    node n;
    scene.root.add(n);
    auto owned = std::make_unique<rendering_engine::directional_light>();
    const rendering_engine::directional_light* l = owned.get();
    n.add_component<light_component>(light_component{std::move(owned)});

    // An identity-oriented node shines along the engine forward, +X, not down.
    scene.update();
    EXPECT_NEAR(l->direction.x, 1.0f, k_eps);
    EXPECT_NEAR(l->direction.y, 0.0f, k_eps);
    EXPECT_NEAR(l->direction.z, 0.0f, k_eps);

    // look_at aims it: straight down at a point below the node.
    n.transform.set_position(vec3{0.0f, 0.0f, 5.0f});
    n.look_at(vec3{0.0f, 0.0f, 0.0f});
    scene.update();
    EXPECT_NEAR(l->direction.x, 0.0f, k_eps);
    EXPECT_NEAR(l->direction.y, 0.0f, k_eps);
    EXPECT_NEAR(l->direction.z, -1.0f, k_eps);

    // A scaled node still yields a unit direction.
    n.transform.set_scale(vec3{4.0f, 4.0f, 4.0f});
    scene.update();
    EXPECT_NEAR(l->direction.z, -1.0f, k_eps);
}

TEST(light_component, a_zero_scale_node_keeps_the_last_direction_instead_of_nan)
{
    runtime::scene scene;
    node n;
    scene.root.add(n);
    auto owned = std::make_unique<rendering_engine::directional_light>();
    const rendering_engine::directional_light* l = owned.get();
    n.add_component<light_component>(light_component{std::move(owned)});

    n.look_at(vec3{0.0f, 1.0f, 0.0f});
    scene.update();
    ASSERT_NEAR(l->direction.y, 1.0f, k_eps);

    n.transform.set_scale(vec3{0.0f, 0.0f, 0.0f});
    scene.update();
    EXPECT_FALSE(std::isnan(l->direction.x));
    EXPECT_FALSE(std::isnan(l->direction.y));
    EXPECT_FALSE(std::isnan(l->direction.z));
    EXPECT_NEAR(l->direction.y, 1.0f, k_eps);
}

TEST(light_component, removing_the_component_unregisters_the_light)
{
    runtime::scene scene;
    node n;
    scene.root.add(n);
    auto owned = std::make_unique<point_light>();
    const light* l = owned.get();
    n.add_component<light_component>(light_component{std::move(owned)});
    ASSERT_TRUE(is_registered(l));

    n.remove_component<light_component>();
    EXPECT_FALSE(is_registered(l));
}
