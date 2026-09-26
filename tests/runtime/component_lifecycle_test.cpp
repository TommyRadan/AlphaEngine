// Unit tests for the component lifecycle hooks a runtime::node dispatches —
// on_attach / on_update / on_active_changed / on_destroy — asserted as an
// ordered sequence rather than as counts (docs/scene_graph.md, "Lifecycle
// hooks"): the order for one component, what add_component to a disabled
// node and a same-type replacement fire, parents-before-children update
// order with a disabled subtree skipped, what ~node does (frees components,
// detaches from its parent, orphans its children with their components
// intact), and node::look_at as a root, under a translated parent, under a
// rotated parent, and at its own position. All device-free.

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include <core/math/mat4.hpp>
#include <core/math/vec3.hpp>
#include <core/math/vec4.hpp>
#include <runtime/component.hpp>
#include <runtime/node.hpp>
#include <runtime/scene_graph.hpp>
#include <support/recording_component.hpp>

using core::math::vec3;
using core::math::vec4;
using runtime::component_store;
using runtime::context;
using runtime::node;
using test_support::hook_log;
using test_support::recorder;
using test_support::recording_component;

namespace
{
    constexpr float k_eps = 1e-4f;
    constexpr float k_pi = 3.14159265358979323846f;

    using events = std::vector<std::string>;

    // The node's forward (-Z) axis in world space, read off its world matrix
    // so the answer includes every ancestor's rotation.
    vec3 world_forward(const node& n)
    {
        const vec4 forward = n.world_matrix() * vec4{0.0f, 0.0f, -1.0f, 0.0f};
        return core::math::normalize(vec3{forward.x, forward.y, forward.z});
    }

    void expect_vec3_near(const vec3& actual, const vec3& expected, float eps = k_eps)
    {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }
} // namespace

// -- hook order for one component -------------------------------------------

TEST(component_lifecycle, hooks_fire_in_attach_update_active_destroy_order)
{
    context scene;
    hook_log log;
    node n;
    scene.root.add(n);

    n.add_component<recording_component>(recorder(log));
    EXPECT_EQ(log.events, (events{"attach"}));

    scene.update();
    EXPECT_EQ(log.events, (events{"attach", "update"}));

    n.set_active(false);
    n.set_active(true);
    EXPECT_EQ(log.events, (events{"attach", "update", "active:false", "active:true"}));

    n.remove_component<recording_component>();
    EXPECT_EQ(log.events, (events{"attach", "update", "active:false", "active:true", "destroy"}));

    // Nothing fires for a component that is gone.
    scene.update();
    n.set_active(false);
    EXPECT_EQ(log.events.size(), 5u);
}

TEST(component_lifecycle, on_attach_sees_the_owning_node)
{
    context scene;
    node n;
    n.name = "owner";
    scene.root.add(n);

    // The hook receives the node the component was added to, already
    // wired into the tree, so registrations keyed off it (its transform,
    // its scene) are valid straight away.
    const node* seen = nullptr;
    struct attach_probe
    {
        const node** seen;
        void on_attach(node& owner)
        {
            *seen = &owner;
        }
    };
    n.add_component<attach_probe>(attach_probe{&seen});
    EXPECT_EQ(seen, &n);
    EXPECT_EQ(n.scene(), &scene);
}

TEST(component_lifecycle, adding_to_a_disabled_node_attaches_then_hides)
{
    context scene;
    hook_log log;
    node n;
    scene.root.add(n);
    n.set_active(false);

    // Documented as "also on add_component to a disabled node, with false":
    // the component is told to hide right after it attaches, so it never
    // shows for a subtree that is not visible.
    n.add_component<recording_component>(recorder(log));
    EXPECT_EQ(log.events, (events{"attach", "active:false"}));

    n.set_active(true);
    EXPECT_EQ(log.events, (events{"attach", "active:false", "active:true"}));
}

TEST(component_lifecycle, adding_under_a_disabled_ancestor_attaches_then_hides)
{
    context scene;
    hook_log log;
    node parent;
    node child;
    scene.root.add(parent);
    parent.add(child);
    parent.set_active(false);

    // The child's own flag is still true; only its effective state is off.
    child.add_component<recording_component>(recorder(log));
    EXPECT_EQ(log.events, (events{"attach", "active:false"}));
}

TEST(component_lifecycle, replacing_a_component_destroys_the_old_one_before_attaching_the_new)
{
    context scene;
    hook_log log;
    node n;
    scene.root.add(n);

    n.add_component<recording_component>(recorder(log, "old", 1));
    recording_component* replacement = n.add_component<recording_component>(recorder(log, "new", 2));
    ASSERT_NE(replacement, nullptr);
    EXPECT_EQ(replacement->value, 2);
    EXPECT_EQ(n.get_component<recording_component>()->value, 2);

    EXPECT_EQ(log.events, (events{"old.attach", "old.destroy", "new.attach"}));
    EXPECT_EQ(log.attaches, 2);
    EXPECT_EQ(log.destroys, 1);
}

TEST(component_lifecycle, remove_all_components_destroys_each_one)
{
    struct other_component
    {
        hook_log* log;
        void on_destroy()
        {
            log->events.push_back("other.destroy");
        }
    };

    context scene;
    hook_log log;
    node n;
    scene.root.add(n);
    n.add_component<recording_component>(recorder(log, "rec"));
    n.add_component<other_component>(other_component{&log});

    n.remove_all_components();
    EXPECT_FALSE(n.has_component<recording_component>());
    EXPECT_FALSE(n.has_component<other_component>());
    // Freed in the order they were added.
    EXPECT_EQ(log.events, (events{"rec.attach", "rec.destroy", "other.destroy"}));

    EXPECT_NO_THROW(n.remove_all_components()); // nothing left: a no-op
    EXPECT_EQ(log.events.size(), 3u);
}

// -- moving nodes between active states -------------------------------------

TEST(component_lifecycle, moving_under_a_disabled_parent_hides_and_moving_out_shows)
{
    context scene;
    hook_log log;
    node dark;
    node lit;
    node mover;
    scene.root.add(dark);
    scene.root.add(lit);
    scene.root.add(mover);
    mover.add_component<recording_component>(recorder(log));
    dark.set_active(false);
    ASSERT_EQ(log.events, (events{"attach"}));

    dark.add(mover);
    EXPECT_FALSE(mover.is_effective_active());
    EXPECT_EQ(log.events, (events{"attach", "active:false"}));

    // Re-parenting straight from a disabled parent to an enabled one flips
    // once, to true — it does not bounce through world space first.
    lit.add(mover);
    EXPECT_TRUE(mover.is_effective_active());
    EXPECT_EQ(log.events, (events{"attach", "active:false", "active:true"}));

    // Moving between two enabled parents is not a change at all.
    scene.root.add(mover);
    EXPECT_EQ(log.events.size(), 3u);
}

TEST(component_lifecycle, disabling_a_subtree_notifies_parents_before_children)
{
    context scene;
    hook_log log;
    node parent;
    node child;
    node grandchild;
    scene.root.add(parent);
    parent.add(child);
    child.add(grandchild);
    parent.add_component<recording_component>(recorder(log, "p"));
    child.add_component<recording_component>(recorder(log, "c"));
    grandchild.add_component<recording_component>(recorder(log, "g"));
    log.events.clear();

    parent.set_active(false);
    EXPECT_EQ(log.events, (events{"p.active:false", "c.active:false", "g.active:false"}));

    // A node whose effective state did not change is not told again: with
    // the parent still off, toggling the child's own flag reaches no one.
    log.events.clear();
    child.set_active(false);
    child.set_active(true);
    EXPECT_TRUE(log.events.empty());

    parent.set_active(true);
    EXPECT_EQ(log.events, (events{"p.active:true", "c.active:true", "g.active:true"}));
}

// -- update order and inactive subtrees -------------------------------------

TEST(component_lifecycle, update_visits_parents_before_children_in_insertion_order)
{
    context scene;
    hook_log log;
    node parent;
    node first;
    node second;
    node grandchild;
    scene.root.add(parent);
    parent.add(first);
    parent.add(second);
    first.add(grandchild);
    parent.add_component<recording_component>(recorder(log, "p"));
    first.add_component<recording_component>(recorder(log, "c1"));
    second.add_component<recording_component>(recorder(log, "c2"));
    grandchild.add_component<recording_component>(recorder(log, "g"));
    log.events.clear();

    // Depth-first: a node updates before its children, and a subtree is
    // finished before the next sibling starts, so a component can rely on
    // its parent's on_update having already run.
    scene.update();
    EXPECT_EQ(log.events, (events{"p.update", "c1.update", "g.update", "c2.update"}));
}

TEST(component_lifecycle, update_skips_a_disabled_subtree_but_not_its_siblings)
{
    context scene;
    hook_log log;
    node parent;
    node first;
    node second;
    node grandchild;
    scene.root.add(parent);
    parent.add(first);
    parent.add(second);
    first.add(grandchild);
    parent.add_component<recording_component>(recorder(log, "p"));
    first.add_component<recording_component>(recorder(log, "c1"));
    second.add_component<recording_component>(recorder(log, "c2"));
    grandchild.add_component<recording_component>(recorder(log, "g"));

    first.set_active(false);
    log.events.clear();
    scene.update();
    // The grandchild's own flag is still true; it is skipped because the walk
    // never descends into a disabled node.
    EXPECT_TRUE(grandchild.is_active());
    EXPECT_EQ(log.events, (events{"p.update", "c2.update"}));

    // A disabled root skips the whole scene.
    parent.set_active(false);
    log.events.clear();
    scene.update();
    EXPECT_TRUE(log.events.empty());
}

TEST(component_lifecycle, update_sees_the_settled_world_transform)
{
    context scene;
    hook_log log;
    node parent;
    node child;
    scene.root.add(parent);
    parent.add(child);
    parent.transform.set_position(vec3{10.0f, 0.0f, 0.0f});
    child.transform.set_position(vec3{0.0f, 5.0f, 0.0f});

    vec3 seen{};
    recording_component c = recorder(log);
    c.on_update_action = [&](node& owner) { seen = owner.world_position(); };
    child.add_component<recording_component>(std::move(c));

    scene.update();
    expect_vec3_near(seen, vec3{10.0f, 5.0f, 0.0f});
}

// -- ~node ------------------------------------------------------------------

TEST(component_lifecycle, destroying_a_node_frees_its_components)
{
    context scene;
    hook_log log;
    {
        node n;
        scene.root.add(n);
        n.add_component<recording_component>(recorder(log));
        EXPECT_EQ(log.destroys, 0);
    }
    EXPECT_EQ(log.events, (events{"attach", "destroy"}));
    EXPECT_EQ(log.destroys, 1);

    // The store outlives the node and is still usable: the freed slot is
    // simply recycled by the next add.
    node another;
    scene.root.add(another);
    ASSERT_NE(another.add_component<recording_component>(recorder(log, 9)), nullptr);
    EXPECT_EQ(another.get_component<recording_component>()->value, 9);
}

TEST(component_lifecycle, destroying_a_node_detaches_it_from_its_parent)
{
    context scene;
    node parent;
    node sibling;
    scene.root.add(parent);
    {
        node doomed;
        parent.add(doomed);
        parent.add(sibling);
        ASSERT_EQ(parent.children().size(), 2u);
    }
    // The parent's child list never references the freed node, and the
    // sibling keeps its place.
    ASSERT_EQ(parent.children().size(), 1u);
    EXPECT_EQ(parent.children()[0], &sibling);
    EXPECT_EQ(sibling.parent(), &parent);
    EXPECT_EQ(parent.find("anything"), nullptr);
}

TEST(component_lifecycle, destroying_a_node_orphans_its_children_with_their_components_intact)
{
    context scene;
    hook_log log;
    node child;
    node grandchild;
    {
        node parent;
        scene.root.add(parent);
        parent.add(child);
        child.add(grandchild);
        parent.transform.set_position(vec3{10.0f, 0.0f, 0.0f});
        child.transform.set_position(vec3{1.0f, 2.0f, 3.0f});
        child.add_component<recording_component>(recorder(log, "c"));
        grandchild.add_component<recording_component>(recorder(log, "g"));
        expect_vec3_near(child.world_position(), vec3{11.0f, 2.0f, 3.0f});
    }

    // The child is a root in world space now: its local pose is its world
    // pose, and its own subtree is untouched.
    EXPECT_EQ(child.parent(), nullptr);
    EXPECT_EQ(child.transform.get_parent(), nullptr);
    expect_vec3_near(child.world_position(), vec3{1.0f, 2.0f, 3.0f});
    EXPECT_EQ(grandchild.parent(), &child);
    EXPECT_TRUE(scene.root.children().empty());

    // Orphaning is not destruction: the components survive, still scoped to
    // the scene, and the orphan is walkable on its own.
    EXPECT_EQ(log.destroys, 0);
    EXPECT_EQ(child.store(), &scene.components);
    EXPECT_TRUE(child.has_component<recording_component>());
    EXPECT_TRUE(grandchild.has_component<recording_component>());
    log.events.clear();
    child.update_subtree();
    EXPECT_EQ(log.events, (events{"c.update", "g.update"}));
}

TEST(component_lifecycle, destroying_a_node_that_was_never_scoped_is_harmless)
{
    hook_log log;
    node child;
    {
        node parent; // no store, no scene
        parent.add(child);
        EXPECT_EQ(child.add_component<recording_component>(recorder(log)), nullptr);
    }
    EXPECT_EQ(child.parent(), nullptr);
    EXPECT_EQ(log.attaches, 0);
    EXPECT_EQ(log.destroys, 0);
}

// -- look_at ----------------------------------------------------------------

TEST(component_lifecycle, look_at_on_a_root_points_the_forward_axis_at_the_target)
{
    node n;
    n.transform.set_position(vec3{1.0f, 2.0f, 3.0f});

    n.look_at(vec3{1.0f, 2.0f, -7.0f}, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(world_forward(n), vec3{0.0f, 0.0f, -1.0f});

    n.look_at(vec3{6.0f, 2.0f, 3.0f}, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(world_forward(n), vec3{1.0f, 0.0f, 0.0f});
    // Turning does not move the node.
    expect_vec3_near(n.world_position(), vec3{1.0f, 2.0f, 3.0f});

    // Any direction, not just an axis: forward is the unit vector to the target.
    const vec3 target{4.0f, -1.0f, 5.0f};
    n.look_at(target, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(world_forward(n), core::math::normalize(target - n.world_position()));
}

TEST(component_lifecycle, look_at_keeps_the_given_up_axis_upright)
{
    node n;
    n.look_at(vec3{5.0f, 0.0f, 0.0f}, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(n.transform.get_up(), vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(n.transform.get_right(), vec3{0.0f, 0.0f, 1.0f});

    // The default up is +Z, so the same target rolls the node onto it.
    node z_up;
    z_up.look_at(vec3{5.0f, 0.0f, 0.0f});
    expect_vec3_near(world_forward(z_up), vec3{1.0f, 0.0f, 0.0f});
    expect_vec3_near(z_up.transform.get_up(), vec3{0.0f, 0.0f, 1.0f});
}

TEST(component_lifecycle, look_at_under_a_translated_parent_aims_in_world_space)
{
    node parent;
    node child;
    parent.add(child);
    parent.transform.set_position(vec3{10.0f, 0.0f, 0.0f});
    child.transform.set_position(vec3{0.0f, 5.0f, 0.0f});
    ASSERT_NEAR(child.world_position().x, 10.0f, k_eps);

    // Straight ahead of the child's *world* position. Read as a local target
    // it would sit at (10, 0, -8) from the child instead, so this only comes
    // out as -Z when the parent's offset is taken out first.
    const vec3 target{10.0f, 5.0f, -8.0f};
    child.look_at(target, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(world_forward(child), vec3{0.0f, 0.0f, -1.0f});

    const vec3 diagonal{0.0f, 5.0f, -10.0f};
    child.look_at(diagonal, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(world_forward(child), core::math::normalize(diagonal - child.world_position()));
}

TEST(component_lifecycle, look_at_under_a_rotated_parent_still_aims_forward_at_the_target)
{
    // Documented as exact for the forward axis (the target is re-expressed
    // in the parent's frame, and the parent's rotation composes back out);
    // only the up handling is approximate under a rotated ancestor.
    node parent;
    node child;
    parent.add(child);
    parent.transform.set_position(vec3{0.0f, 0.0f, 10.0f});
    parent.transform.set_rotation(vec3{0.0f, k_pi * 0.5f, 0.0f}); // 90 degrees about y
    child.transform.set_position(vec3{2.0f, 0.0f, 0.0f});

    const vec3 target{-5.0f, 3.0f, 4.0f};
    child.look_at(target, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(world_forward(child), core::math::normalize(target - child.world_position()), 1e-3f);
}

TEST(component_lifecycle, look_at_the_node_own_position_leaves_the_orientation_alone)
{
    node n;
    n.transform.set_position(vec3{1.0f, 1.0f, 1.0f});
    n.look_at(vec3{1.0f, 1.0f, -4.0f}, vec3{0.0f, 1.0f, 0.0f});
    n.look_at(vec3{6.0f, 1.0f, 1.0f}, vec3{0.0f, 1.0f, 0.0f});
    const vec3 before = world_forward(n);

    // A zero direction has no orientation to give: the call is a no-op
    // rather than a NaN rotation.
    n.look_at(vec3{1.0f, 1.0f, 1.0f}, vec3{0.0f, 1.0f, 0.0f});
    expect_vec3_near(world_forward(n), before);
    expect_vec3_near(world_forward(n), vec3{1.0f, 0.0f, 0.0f});
}
