// Unit tests for runtime::node: the transform hierarchy and cached world
// matrices, parent/child links and re-parenting, find(), active/effective-active
// flags, and the component store attach/get/remove path. All device-free.

#include <gtest/gtest.h>

#include <core/math/mat4.hpp>
#include <core/math/vec3.hpp>
#include <runtime/component.hpp>
#include <runtime/node.hpp>

using core::math::vec3;
using runtime::component_store;
using runtime::node;

namespace
{
    constexpr float k_eps = 1e-4f;

    // Translation column of a column-major world matrix.
    vec3 translation_of(const core::math::mat4& m)
    {
        return vec3{m.m[12], m.m[13], m.m[14]};
    }

    // A plain-data component: no on_attach/on_update/on_active_changed hooks.
    struct tag_component
    {
        int value{0};
    };
}

// -- hierarchy links --------------------------------------------------------

TEST(node, default_node_is_a_root_with_no_children)
{
    node n;
    EXPECT_EQ(n.parent(), nullptr);
    EXPECT_TRUE(n.children().empty());
}

TEST(node, add_links_parent_and_child)
{
    node parent;
    node child;
    parent.add(child);
    EXPECT_EQ(child.parent(), &parent);
    ASSERT_EQ(parent.children().size(), 1u);
    EXPECT_EQ(parent.children()[0], &child);
}

TEST(node, add_is_idempotent_on_reparenting)
{
    node a;
    node b;
    node child;
    a.add(child);
    b.add(child); // moves the child from a to b
    EXPECT_EQ(child.parent(), &b);
    EXPECT_TRUE(a.children().empty());
    ASSERT_EQ(b.children().size(), 1u);
    EXPECT_EQ(b.children()[0], &child);
}

TEST(node, remove_returns_child_to_world_space)
{
    node parent;
    node child;
    parent.transform.set_position(vec3{10.0f, 0.0f, 0.0f});
    parent.add(child);
    parent.remove(child);
    EXPECT_EQ(child.parent(), nullptr);
    EXPECT_TRUE(parent.children().empty());
    // Back in world space the child's world position is just its local one.
    EXPECT_NEAR(child.world_position().x, 0.0f, k_eps);
}

// -- world matrices ---------------------------------------------------------

TEST(node, world_matrix_composes_the_parent_transform)
{
    node parent;
    node child;
    parent.transform.set_position(vec3{10.0f, 0.0f, 0.0f});
    child.transform.set_position(vec3{0.0f, 5.0f, 0.0f});
    parent.add(child);

    const vec3 world = translation_of(child.world_matrix());
    EXPECT_NEAR(world.x, 10.0f, k_eps);
    EXPECT_NEAR(world.y, 5.0f, k_eps);
    EXPECT_NEAR(world.z, 0.0f, k_eps);
}

TEST(node, world_position_tracks_parent_movement)
{
    node parent;
    node child;
    child.transform.set_position(vec3{0.0f, 5.0f, 0.0f});
    parent.add(child);

    // Moving the parent updates the cached world matrix of the child.
    parent.transform.set_position(vec3{1.0f, 2.0f, 3.0f});
    const vec3 world = child.world_position();
    EXPECT_NEAR(world.x, 1.0f, k_eps);
    EXPECT_NEAR(world.y, 7.0f, k_eps);
    EXPECT_NEAR(world.z, 3.0f, k_eps);
}

TEST(node, set_world_position_solves_local_under_parent)
{
    node parent;
    node child;
    parent.transform.set_position(vec3{10.0f, 0.0f, 0.0f});
    parent.add(child);

    child.set_world_position(vec3{10.0f, 5.0f, 0.0f});
    const vec3 world = child.world_position();
    EXPECT_NEAR(world.x, 10.0f, k_eps);
    EXPECT_NEAR(world.y, 5.0f, k_eps);
    EXPECT_NEAR(world.z, 0.0f, k_eps);
}

// -- find -------------------------------------------------------------------

TEST(node, find_locates_descendants_depth_first)
{
    node root;
    root.name = "root";
    node child;
    child.name = "child";
    node grandchild;
    grandchild.name = "grandchild";

    root.add(child);
    child.add(grandchild);

    EXPECT_EQ(root.find("root"), &root); // includes self
    EXPECT_EQ(root.find("child"), &child);
    EXPECT_EQ(root.find("grandchild"), &grandchild);
    EXPECT_EQ(root.find("absent"), nullptr);
}

// -- active flags -----------------------------------------------------------

TEST(node, nodes_start_active)
{
    node n;
    EXPECT_TRUE(n.is_active());
    EXPECT_TRUE(n.is_effective_active());
}

TEST(node, disabling_a_parent_disables_the_subtree_effectively)
{
    node parent;
    node child;
    parent.add(child);

    parent.set_active(false);
    EXPECT_FALSE(parent.is_effective_active());
    // The child's own flag is untouched, but it is not effectively active.
    EXPECT_TRUE(child.is_active());
    EXPECT_FALSE(child.is_effective_active());

    parent.set_active(true);
    EXPECT_TRUE(child.is_effective_active());
}

TEST(node, a_child_disabled_directly_stays_disabled_under_an_active_parent)
{
    node parent;
    node child;
    parent.add(child);
    child.set_active(false);
    EXPECT_TRUE(parent.is_effective_active());
    EXPECT_FALSE(child.is_active());
    EXPECT_FALSE(child.is_effective_active());
}

// -- components -------------------------------------------------------------

TEST(node, add_component_requires_a_store)
{
    node n; // no store attached
    EXPECT_EQ(n.add_component<tag_component>(tag_component{7}), nullptr);
    EXPECT_FALSE(n.has_component<tag_component>());
}

TEST(node, add_then_get_component_round_trips_through_the_store)
{
    component_store store;
    node n;
    n.set_store(&store);

    tag_component* added = n.add_component<tag_component>(tag_component{42});
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->value, 42);
    EXPECT_TRUE(n.has_component<tag_component>());

    tag_component* got = n.get_component<tag_component>();
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->value, 42);
    EXPECT_EQ(got, added); // same pooled storage
}

TEST(node, remove_component_clears_it)
{
    component_store store;
    node n;
    n.set_store(&store);
    n.add_component<tag_component>(tag_component{1});
    n.remove_component<tag_component>();
    EXPECT_FALSE(n.has_component<tag_component>());
    EXPECT_EQ(n.get_component<tag_component>(), nullptr);
}

TEST(node, add_component_replaces_an_existing_component_of_the_same_type)
{
    component_store store;
    node n;
    n.set_store(&store);
    n.add_component<tag_component>(tag_component{1});
    tag_component* replaced = n.add_component<tag_component>(tag_component{2});
    ASSERT_NE(replaced, nullptr);
    EXPECT_EQ(replaced->value, 2);
    EXPECT_EQ(n.get_component<tag_component>()->value, 2);
}

TEST(node, add_inherits_the_parent_store)
{
    component_store store;
    node parent;
    parent.set_store(&store);
    node child;
    parent.add(child);
    // The child inherited the store on add, so it can carry components.
    EXPECT_EQ(child.store(), &store);
    EXPECT_NE(child.add_component<tag_component>(tag_component{5}), nullptr);
}

TEST(node, update_subtree_runs_without_components)
{
    node root;
    node child;
    root.add(child);
    EXPECT_NO_THROW(root.update_subtree());
}
