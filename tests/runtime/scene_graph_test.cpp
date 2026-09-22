// Unit tests for runtime::context: the per-frame traversal (effective-active
// gating), the deferred command queue (destroy / remove_component / reparent /
// set_active from inside a component hook), on_destroy dispatch when a store
// dies with components in it, ancestor-cycle rejection in node::add, and
// component migration when a populated node is re-parented across scenes.
// All device-free.

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <vector>

#include <runtime/component.hpp>
#include <runtime/node.hpp>
#include <runtime/scene_graph.hpp>

using runtime::component_store;
using runtime::context;
using runtime::node;

namespace
{
    // What a component saw, shared by every copy of it (components are moved
    // between pool slots and stores, so the log lives outside them).
    struct hook_log
    {
        int attaches{0};
        int updates{0};
        int destroys{0};
        std::vector<bool> active_changes;
    };

    // A component implementing every hook, with a pluggable on_update body so
    // a test can make it act on its node from inside the traversal.
    struct recording_component
    {
        hook_log* log{nullptr};
        int value{0};
        std::function<void(node&)> on_update_action;

        void on_attach(node&)
        {
            ++log->attaches;
        }

        void on_update(node& owner)
        {
            ++log->updates;
            if (on_update_action)
            {
                on_update_action(owner);
            }
        }

        void on_active_changed(node&, bool active)
        {
            log->active_changes.push_back(active);
        }

        void on_destroy()
        {
            ++log->destroys;
        }
    };

    recording_component recorder(hook_log& log, int value = 0)
    {
        recording_component c;
        c.log = &log;
        c.value = value;
        return c;
    }
} // namespace

// -- construction -----------------------------------------------------------

TEST(scene_graph, root_is_scoped_to_the_scene_store_and_knows_its_scene)
{
    context scene;
    EXPECT_EQ(scene.root.store(), &scene.components);
    EXPECT_EQ(scene.components.scene(), &scene);
    EXPECT_EQ(scene.root.scene(), &scene);

    node child;
    scene.root.add(child);
    EXPECT_EQ(child.scene(), &scene);

    node unscoped;
    EXPECT_EQ(unscoped.scene(), nullptr);
    component_store bare;
    EXPECT_EQ(bare.scene(), nullptr);
}

// -- traversal --------------------------------------------------------------

TEST(scene_graph, update_dispatches_on_update_only_to_effectively_active_nodes)
{
    context scene;
    hook_log parent_log;
    hook_log child_log;
    node parent;
    node child;
    scene.root.add(parent);
    parent.add(child);
    parent.add_component<recording_component>(recorder(parent_log));
    child.add_component<recording_component>(recorder(child_log));

    scene.update();
    EXPECT_EQ(parent_log.updates, 1);
    EXPECT_EQ(child_log.updates, 1);

    // The child's own flag stays true, but it hangs off a disabled parent.
    parent.set_active(false);
    scene.update();
    EXPECT_EQ(parent_log.updates, 1);
    EXPECT_EQ(child_log.updates, 1);

    parent.set_active(true);
    scene.update();
    EXPECT_EQ(parent_log.updates, 2);
    EXPECT_EQ(child_log.updates, 2);
}

TEST(scene_graph, a_node_removed_from_a_disabled_parent_is_active_again_as_a_root)
{
    context scene;
    hook_log log;
    node parent;
    node child;
    scene.root.add(parent);
    parent.add(child);
    child.add_component<recording_component>(recorder(log));

    parent.set_active(false);
    ASSERT_FALSE(child.is_effective_active());
    ASSERT_EQ(log.active_changes, (std::vector<bool>{false}));

    parent.remove(child);
    EXPECT_TRUE(child.is_effective_active());
    EXPECT_EQ(log.active_changes, (std::vector<bool>{false, true}));

    // As a root it is walkable again.
    child.update_subtree();
    EXPECT_EQ(log.updates, 1);
}

TEST(scene_graph, children_orphaned_by_a_dying_disabled_parent_are_active_again)
{
    context scene;
    hook_log log;
    node child;
    {
        node parent;
        scene.root.add(parent);
        parent.add(child);
        child.add_component<recording_component>(recorder(log));
        parent.set_active(false);
        ASSERT_FALSE(child.is_effective_active());
    }
    EXPECT_EQ(child.parent(), nullptr);
    EXPECT_TRUE(child.is_effective_active());
    EXPECT_EQ(log.active_changes, (std::vector<bool>{false, true}));
}

TEST(scene_graph, is_traversing_is_true_only_inside_the_walk)
{
    context scene;
    hook_log log;
    node n;
    scene.root.add(n);
    bool seen_traversing = false;
    recording_component c = recorder(log);
    c.on_update_action = [&](node& owner) { seen_traversing = owner.scene()->is_traversing(); };
    n.add_component<recording_component>(std::move(c));

    EXPECT_FALSE(scene.is_traversing());
    scene.update();
    EXPECT_TRUE(seen_traversing);
    EXPECT_FALSE(scene.is_traversing());
}

// -- deferred commands ------------------------------------------------------

TEST(scene_graph, defer_destroy_from_on_update_detaches_frees_components_and_releases)
{
    context scene;
    hook_log log;
    auto owned = std::make_unique<node>();
    node* raw = owned.get();
    raw->name = "doomed";
    scene.root.add(*raw);

    int released = 0;
    bool was_attached_during_update = false;
    bool was_pending_during_update = false;
    recording_component c = recorder(log);
    c.on_update_action = [&](node& owner)
    {
        owner.scene()->defer_destroy(owner,
                                     [&]
                                     {
                                         ++released;
                                         owned.reset();
                                     });
        // Nothing happens until the traversal has unwound.
        was_attached_during_update = owner.parent() == &scene.root;
        was_pending_during_update = owner.is_destroy_pending();
    };
    raw->add_component<recording_component>(std::move(c));

    scene.update();

    EXPECT_TRUE(was_attached_during_update);
    EXPECT_TRUE(was_pending_during_update);
    EXPECT_TRUE(scene.root.children().empty());
    EXPECT_EQ(log.updates, 1);
    EXPECT_EQ(log.destroys, 1);
    EXPECT_EQ(released, 1);
    EXPECT_EQ(owned, nullptr);
    EXPECT_EQ(scene.pending_command_count(), 0u);
}

TEST(scene_graph, defer_destroy_frees_the_components_of_the_whole_subtree)
{
    context scene;
    hook_log log;
    node parent;
    node child;
    node grandchild;
    scene.root.add(parent);
    parent.add(child);
    child.add(grandchild);
    parent.add_component<recording_component>(recorder(log));
    child.add_component<recording_component>(recorder(log));
    grandchild.add_component<recording_component>(recorder(log));

    scene.defer_destroy(parent);
    EXPECT_TRUE(parent.is_destroy_pending());
    EXPECT_EQ(log.destroys, 0);

    scene.apply_deferred();

    // Detached from the scene with its links intact, stripped of components.
    EXPECT_EQ(parent.parent(), nullptr);
    EXPECT_TRUE(scene.root.children().empty());
    EXPECT_EQ(child.parent(), &parent);
    EXPECT_EQ(grandchild.parent(), &child);
    EXPECT_EQ(log.destroys, 3);
    EXPECT_FALSE(parent.has_component<recording_component>());
    EXPECT_FALSE(child.has_component<recording_component>());
    EXPECT_FALSE(grandchild.has_component<recording_component>());
    EXPECT_FALSE(parent.is_destroy_pending());
}

TEST(scene_graph, defer_destroy_ignores_a_node_already_pending)
{
    context scene;
    node n;
    scene.root.add(n);
    int released = 0;
    scene.defer_destroy(n, [&] { ++released; });
    scene.defer_destroy(n, [&] { ++released; });
    EXPECT_EQ(scene.pending_command_count(), 1u);
    scene.apply_deferred();
    EXPECT_EQ(released, 1);
}

TEST(scene_graph, defer_set_active_applies_after_the_traversal)
{
    context scene;
    hook_log log;
    node n;
    scene.root.add(n);
    bool active_during_update = false;
    recording_component c = recorder(log);
    c.on_update_action = [&](node& owner)
    {
        owner.scene()->defer_set_active(owner, false);
        active_during_update = owner.is_active();
    };
    n.add_component<recording_component>(std::move(c));

    scene.update();
    EXPECT_TRUE(active_during_update);
    EXPECT_FALSE(n.is_active());
    EXPECT_EQ(log.active_changes, (std::vector<bool>{false}));

    // Disabled now, so the next walk skips it.
    scene.update();
    EXPECT_EQ(log.updates, 1);
}

TEST(scene_graph, defer_reparent_moves_or_detaches_after_the_traversal)
{
    context scene;
    hook_log log;
    node a;
    node b;
    node mover;
    scene.root.add(a);
    scene.root.add(b);
    a.add(mover);
    recording_component c = recorder(log);
    c.on_update_action = [&](node& owner) { owner.scene()->defer_reparent(owner, &b); };
    mover.add_component<recording_component>(std::move(c));

    scene.update();
    EXPECT_EQ(mover.parent(), &b);
    EXPECT_TRUE(a.children().empty());
    ASSERT_EQ(b.children().size(), 1u);
    EXPECT_EQ(b.children()[0], &mover);

    scene.defer_reparent(mover, nullptr);
    scene.apply_deferred();
    EXPECT_EQ(mover.parent(), nullptr);
    EXPECT_TRUE(b.children().empty());
}

TEST(scene_graph, defer_remove_component_frees_the_component_after_the_traversal)
{
    context scene;
    hook_log log;
    node n;
    scene.root.add(n);
    bool had_component_during_update = false;
    recording_component c = recorder(log);
    c.on_update_action = [&](node& owner)
    {
        owner.scene()->defer_remove_component<recording_component>(owner);
        had_component_during_update = owner.has_component<recording_component>();
    };
    n.add_component<recording_component>(std::move(c));

    scene.update();
    EXPECT_TRUE(had_component_during_update);
    EXPECT_FALSE(n.has_component<recording_component>());
    EXPECT_EQ(log.destroys, 1);
}

TEST(scene_graph, commands_run_in_queue_order_and_may_queue_more)
{
    context scene;
    std::vector<int> order;
    scene.defer(
        [&]
        {
            order.push_back(1);
            scene.defer([&] { order.push_back(3); });
        });
    scene.defer([&] { order.push_back(2); });
    EXPECT_EQ(scene.pending_command_count(), 2u);

    scene.apply_deferred();
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
    EXPECT_EQ(scene.pending_command_count(), 0u);
}

TEST(scene_graph, commands_queued_outside_a_traversal_run_at_the_next_update)
{
    context scene;
    bool ran = false;
    scene.defer([&] { ran = true; });
    EXPECT_FALSE(ran);
    scene.update();
    EXPECT_TRUE(ran);
}

// -- store teardown ---------------------------------------------------------

TEST(scene_graph, destroying_a_store_dispatches_on_destroy_to_every_live_component)
{
    hook_log log;
    {
        component_store store;
        store.add<recording_component>(recorder(log));
        store.add<recording_component>(recorder(log));
        EXPECT_EQ(log.destroys, 0);
    }
    EXPECT_EQ(log.destroys, 2);
}

TEST(scene_graph, a_scene_dying_with_nodes_attached_frees_and_unscopes_them)
{
    hook_log log;
    node survivor;
    node grandchild;
    {
        context scene;
        scene.root.add(survivor);
        survivor.add(grandchild);
        survivor.add_component<recording_component>(recorder(log));
        grandchild.add_component<recording_component>(recorder(log));
    }
    // on_destroy ran while the store was alive, and the outliving nodes no
    // longer point at it, so destroying them later is safe.
    EXPECT_EQ(log.destroys, 2);
    EXPECT_EQ(survivor.parent(), nullptr);
    EXPECT_EQ(survivor.store(), nullptr);
    EXPECT_EQ(grandchild.store(), nullptr);
    EXPECT_FALSE(survivor.has_component<recording_component>());
    EXPECT_FALSE(grandchild.has_component<recording_component>());
    // The subtree itself is left intact for its owner.
    EXPECT_EQ(grandchild.parent(), &survivor);
}

TEST(scene_graph, unscoping_a_node_frees_its_components)
{
    hook_log log;
    component_store store;
    node n;
    n.set_store(&store);
    n.add_component<recording_component>(recorder(log));

    n.set_store(nullptr);
    EXPECT_EQ(n.store(), nullptr);
    EXPECT_FALSE(n.has_component<recording_component>());
    EXPECT_EQ(log.destroys, 1);
}

// -- cycles -----------------------------------------------------------------

TEST(scene_graph, add_rejects_an_ancestor_cycle)
{
    node a;
    node b;
    node c;
    a.add(b);
    b.add(c);

    c.add(a); // would close a -> b -> c -> a
    EXPECT_EQ(a.parent(), nullptr);
    EXPECT_TRUE(c.children().empty());
    EXPECT_EQ(c.parent(), &b);

    b.add(a); // direct parent of the would-be child
    EXPECT_EQ(a.parent(), nullptr);
    ASSERT_EQ(b.children().size(), 1u);
    EXPECT_EQ(b.children()[0], &c);

    a.add(a);
    EXPECT_EQ(a.parent(), nullptr);
    ASSERT_EQ(a.children().size(), 1u);
    EXPECT_EQ(a.children()[0], &b);
}

// -- cross-store reparent ---------------------------------------------------

TEST(scene_graph, reparenting_across_scenes_migrates_the_components)
{
    hook_log log;
    auto scene_a = std::make_unique<context>();
    context scene_b;
    node n;
    scene_a->root.add(n);
    n.add_component<recording_component>(recorder(log, 7));
    ASSERT_EQ(log.attaches, 1);

    scene_b.root.add(n);
    EXPECT_EQ(n.store(), &scene_b.components);
    EXPECT_EQ(n.scene(), &scene_b);
    ASSERT_TRUE(n.has_component<recording_component>());
    ASSERT_NE(n.get_component<recording_component>(), nullptr);
    EXPECT_EQ(n.get_component<recording_component>()->value, 7);
    // A move, not a destroy-and-recreate: no hook fired.
    EXPECT_EQ(log.attaches, 1);
    EXPECT_EQ(log.destroys, 0);

    // Nothing was left behind in the old scene's pools.
    scene_a.reset();
    EXPECT_EQ(log.destroys, 0);

    // The component is genuinely alive in the new scene.
    scene_b.update();
    EXPECT_EQ(log.updates, 1);
    n.remove_component<recording_component>();
    EXPECT_EQ(log.destroys, 1);
}

TEST(scene_graph, reparenting_across_scenes_migrates_the_whole_subtree)
{
    hook_log log;
    context scene_a;
    context scene_b;
    node parent;
    node child;
    scene_a.root.add(parent);
    parent.add(child);
    child.add_component<recording_component>(recorder(log, 3));

    scene_b.root.add(parent);
    EXPECT_EQ(child.store(), &scene_b.components);
    ASSERT_NE(child.get_component<recording_component>(), nullptr);
    EXPECT_EQ(child.get_component<recording_component>()->value, 3);
    EXPECT_EQ(log.destroys, 0);
}

TEST(scene_graph, a_parent_without_a_store_leaves_the_child_store_alone)
{
    hook_log log;
    context scene;
    node group; // never scoped
    node n;
    scene.root.add(n);
    n.add_component<recording_component>(recorder(log));

    group.add(n);
    EXPECT_EQ(n.parent(), &group);
    EXPECT_EQ(n.store(), &scene.components);
    EXPECT_TRUE(n.has_component<recording_component>());
    EXPECT_EQ(log.destroys, 0);
}

// -- traversal guard --------------------------------------------------------

#ifndef NDEBUG
// Debug builds refuse an immediate structural mutation from inside a hook
// outright; the release-build fallback (log and defer) is the same queue the
// explicit defer_* tests above exercise.
TEST(scene_graph_death, immediate_mutation_from_a_hook_asserts_in_debug_builds)
{
    auto mutate_during_update = []
    {
        context scene;
        hook_log log;
        node n;
        scene.root.add(n);
        recording_component c = recorder(log);
        c.on_update_action = [](node& owner) { owner.set_active(false); };
        n.add_component<recording_component>(std::move(c));
        scene.update();
    };
    EXPECT_DEATH(mutate_during_update(), "traversal");
}
#endif
