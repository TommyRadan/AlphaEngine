/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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

#include <runtime/scene_graph.hpp>

#include <string>
#include <utility>

#include <core/log.hpp>

namespace
{
    // Frees the components of @p target and every node below it. The links
    // are left intact so the subtree stays a coherent unit for its owner to
    // release (or reuse).
    void release_subtree_components(runtime::node& target)
    {
        target.remove_all_components();
        for (runtime::node* child : target.children())
        {
            release_subtree_components(*child);
        }
    }

    // Commands may queue further commands; the drain repeats until quiet. A
    // command that keeps re-queueing itself would never let a frame end, so
    // the drain gives up after this many rounds and leaves the remainder for
    // the next update.
    constexpr int k_max_drain_rounds = 32;
} // namespace

runtime::context::traversal_scope::traversal_scope(context* scene) noexcept : m_scene{scene}
{
    if (m_scene != nullptr)
    {
        ++m_scene->m_traversal_depth;
    }
}

runtime::context::traversal_scope::~traversal_scope()
{
    if (m_scene != nullptr)
    {
        --m_scene->m_traversal_depth;
    }
}

runtime::context::context()
{
    // Wire the root to the scene's component store so every node added under it
    // inherits the store (via node::add) and can carry components, and point
    // the store back here so nodes can reach the deferred command queue.
    components.m_scene = this;
    root.set_store(&components);
}

runtime::context::~context()
{
    if (!m_pending.empty())
    {
        // Applying them now would touch nodes whose owners may already be
        // tearing down; dropping is the safe choice, but it is worth a note.
        LOG_WRN("runtime::context: destroyed with %zu deferred command(s) never applied", m_pending.size());
    }

    // Nodes are caller-owned and may outlive the scene. Anything still
    // attached is cut loose now, while the store is alive: its components are
    // freed (with on_destroy) and the subtree is unscoped, so a node destroyed
    // later never reaches into this store. Normally the owners have already
    // released their nodes (the demos do so on engine_stop) and this is a
    // no-op.
    if (!root.children().empty())
    {
        LOG_WRN("runtime::context: %zu subtree(s) still attached at teardown; freeing their components",
                root.children().size());
    }
    std::vector<node*> attached = root.children();
    for (node* child : attached)
    {
        root.remove(*child);
        child->set_store(nullptr);
    }
}

void runtime::context::init()
{
    LOG_INF("Init Scene Graph");
    // The root node is always present; subtrees parent under it via node::add.
    // Node add/remove and traversal diagnostics are expected to be logged from
    // scene_graph API calls. See docs/logging.md.
}

void runtime::context::quit()
{
    LOG_INF("Quit Scene Graph");
}

void runtime::context::update()
{
    // One serial depth-first walk. Each component's on_update runs against
    // world matrices that transform::get_world_matrix resolves lazily (and
    // caches) as the walk reaches them, parents before children.
    //
    // An earlier version warmed those caches first with a parallel_for over
    // each depth band of the tree. It was removed without a replacement: the
    // per-node work is a version compare and one 4x4 multiply, far cheaper
    // than the std::function allocation, queue push and worker wake it cost
    // to farm out, and no measurement ever showed the scene sizes this engine
    // draws gaining from it. Its race-freedom also rested on invariants
    // nothing enforces — that every transform's parent is the transform of a
    // node one band up — while transform::set_parent is public and
    // mesh_component already parents a non-node transform. Bring parallelism
    // back here only against a measured workload, with those ownership rules
    // made explicit first.
    //
    // Components bridge to shared subsystems (renderer registries, the light
    // list, per-draw GPU buffers), none of which are thread-safe, so the walk
    // itself must stay on the main thread regardless.
    root.update_subtree();

    // The walk is over, so the tree may change again: apply what the hooks
    // asked for (node destruction, re-parenting, component removal, active
    // toggles) in the order they asked.
    apply_deferred();
}

void runtime::context::defer(std::function<void()> command)
{
    if (!command)
    {
        return;
    }
    m_pending.push_back(std::move(command));
}

void runtime::context::defer_destroy(node& target, std::function<void()> release)
{
    if (target.m_destroy_pending)
    {
        LOG_WRN("runtime::context::defer_destroy: '%s' is already pending destruction; ignoring", target.name.c_str());
        return;
    }
    target.m_destroy_pending = true;

    defer(
        [&target, release = std::move(release)]
        {
            // Unlink first so nothing walks into the subtree once it is gone
            // from the scene, then unwind the components (renderer
            // registrations, lights, cameras) of the whole subtree.
            if (node* parent = target.parent())
            {
                parent->remove(target);
            }
            release_subtree_components(target);
            target.m_destroy_pending = false;

            // Hand the memory back to the owner last. The node must not be
            // touched after this: the owner is free to delete it here.
            if (release)
            {
                release();
            }
        });
}

void runtime::context::defer_reparent(node& target, node* new_parent)
{
    defer(
        [&target, new_parent]
        {
            if (new_parent != nullptr)
            {
                new_parent->add(target);
            }
            else if (node* parent = target.parent())
            {
                parent->remove(target);
            }
        });
}

void runtime::context::defer_set_active(node& target, bool active)
{
    defer([&target, active] { target.set_active(active); });
}

void runtime::context::apply_deferred()
{
    if (is_traversing())
    {
        LOG_ERR("runtime::context::apply_deferred: called during a traversal; commands stay queued");
        return;
    }

    for (int round = 0; round < k_max_drain_rounds && !m_pending.empty(); ++round)
    {
        // Swap the batch out so commands queued while it runs land in a fresh
        // queue for the next round rather than invalidating this walk.
        std::vector<std::function<void()>> batch;
        batch.swap(m_pending);
        for (std::function<void()>& command : batch)
        {
            command();
        }
    }

    if (!m_pending.empty())
    {
        LOG_ERR(
            "runtime::context::apply_deferred: commands kept re-queueing for %d rounds; %zu left for the next update",
            k_max_drain_rounds,
            m_pending.size());
    }
}

std::size_t runtime::context::pending_command_count() const noexcept
{
    return m_pending.size();
}

bool runtime::context::is_traversing() const noexcept
{
    return m_traversal_depth > 0;
}
