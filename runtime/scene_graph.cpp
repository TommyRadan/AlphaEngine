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

#include <algorithm>
#include <unordered_set>
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

    // Copies the local pose (not the parent link) of one transform onto another.
    void copy_pose(const runtime::node& source, runtime::node& target)
    {
        target.transform.set_position(source.transform.get_position());
        target.transform.set_quaternion(source.transform.get_quaternion());
        target.transform.set_scale(source.transform.get_scale());
    }

    // Commands may queue further commands; the drain repeats until quiet. A
    // command that keeps re-queueing itself would never let a frame end, so
    // the drain gives up after this many rounds and leaves the remainder for
    // the next update.
    constexpr int k_max_drain_rounds = 32;

    // Stamps the update walks, unique across every scene in the process so a
    // node that moves between scenes can never carry a mark that happens to
    // match its new scene's current update. Main-thread-only, like the walks.
    uint64_t g_update_stamp = 0;
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

runtime::node& runtime::context::node_pool::allocate(uint32_t& slot)
{
    if (m_free.empty())
    {
        const std::size_t base = m_pages.size() * k_page_size;
        m_pages.push_back(std::make_unique<page>());
        // Pushed in reverse so the new page hands out its lowest slot first.
        for (std::size_t i = k_page_size; i-- > 0;)
        {
            m_free.push_back(static_cast<uint32_t>(base + i));
        }
    }

    const uint32_t index = m_free.back();
    std::optional<node>& target = cell(index);
    target.emplace();
    // Pop only once the node is in place, so a throwing constructor leaves
    // the slot on the free list.
    m_free.pop_back();
    ++m_live;
    slot = index;
    return *target;
}

void runtime::context::node_pool::release(uint32_t slot)
{
    std::optional<node>& target = cell(slot);
    if (!target.has_value())
    {
        return;
    }
    target.reset();
    m_free.push_back(slot);
    --m_live;
}

std::vector<runtime::node*> runtime::context::node_pool::live() const
{
    std::vector<node*> nodes;
    nodes.reserve(m_live);
    for (const std::unique_ptr<page>& p : m_pages)
    {
        for (std::optional<node>& slot : p->slots)
        {
            if (slot.has_value())
            {
                nodes.push_back(&*slot);
            }
        }
    }
    return nodes;
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
        m_pending.clear();
    }

    // Normally quit() has already emptied the scene and this is a no-op.
    release_all();
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
    // Apply what was asked for, then free everything while the subsystems the
    // components unwind against (renderer registries, lights, cameras, GPU
    // buffers) are still up.
    apply_deferred();
    release_all();
}

void runtime::context::update()
{
    {
        // on_update hooks may not restructure the lists walked here; the
        // scope makes the immediate APIs assert (debug) or defer (release).
        traversal_scope traversal{this};

        // One serial depth-first walk settles every world matrix, parents
        // before children, and stamps the nodes it reaches (effectively
        // active, linked under the root) with their place in the walk.
        //
        // An earlier version warmed those caches with a parallel_for over each
        // depth band of the tree. It was removed without a replacement: the
        // per-node work is a version compare and one 4x4 multiply, far cheaper
        // than the std::function allocation, queue push and worker wake it cost
        // to farm out, and no measurement ever showed the scene sizes this
        // engine draws gaining from it. Its race-freedom also rested on
        // invariants nothing enforces — that every transform's parent is the
        // transform of a node one band up — while transform::set_parent is
        // public and mesh_component already parents a non-node transform.
        // Bring parallelism back here only against a measured workload, with
        // those ownership rules made explicit first.
        const uint64_t stamp = ++g_update_stamp;
        uint32_t order = 0;
        propagate(root, stamp, order);

        // Then on_update runs one component type at a time, each a pass over
        // that type's pool, to the components whose owner the walk stamped,
        // in walk order. Components bridge to shared subsystems (renderer
        // registries, the light list, per-draw GPU buffers), none of which are
        // thread-safe, so this stays on the main thread too.
        components.update_visited(stamp);
    }

    // The walk is over, so the tree may change again: apply what the hooks
    // asked for (node destruction, re-parenting, component removal, active
    // toggles) in the order they asked.
    apply_deferred();
}

void runtime::context::propagate(node& target, uint64_t stamp, uint32_t& order)
{
    // A disabled node — or one under a disabled ancestor — freezes its whole
    // subtree: it is not stamped, so none of its components update.
    if (!target.m_effective_active)
    {
        return;
    }
    target.m_visit.stamp = stamp;
    target.m_visit.order = order++;
    (void)target.transform.get_world_matrix();
    for (node* child : target.m_children)
    {
        propagate(*child, stamp, order);
    }
}

runtime::node& runtime::context::create_node(core::string_id name, node* parent)
{
    uint32_t slot = 0;
    node& created = m_nodes.allocate(slot);
    created.m_owning_scene = this;
    created.m_pool_slot = slot;
    created.m_name = name;
    // Scoped here straight away (which also indexes the name), so the node
    // reaches this scene's queue and find() even before it is linked.
    created.set_store(&components);

    node& target = parent != nullptr ? *parent : root;
    context* target_scene = target.scene();
    if (target_scene != nullptr && target_scene->is_traversing())
    {
        // The parent's child list may be under a walk right now; link once
        // that update has unwound.
        target_scene->defer([&target, &created] { target.add(created); });
    }
    else
    {
        target.add(created);
    }
    return created;
}

void runtime::context::destroy_node(node& target)
{
    if (&target == &root)
    {
        LOG_ERR("runtime::context::destroy_node: the scene root cannot be destroyed");
        return;
    }
    if (target.m_owning_scene == nullptr)
    {
        // Caller-owned: unlink it and free its components; the memory stays
        // with the caller.
        defer_destroy(target);
        return;
    }
    if (target.m_owning_scene != this)
    {
        target.m_owning_scene->destroy_node(target);
        return;
    }
    if (target.m_destroy_pending)
    {
        LOG_WRN("runtime::context::destroy_node: '%s' is already pending destruction; ignoring", target.m_name.c_str());
        return;
    }
    target.m_destroy_pending = true;

    defer(
        [this, &target]
        {
            unlink_and_strip(target);
            // The memory goes back once the whole drain is over: a later
            // command in it may still name one of these nodes.
            collect_doomed(target);
        });
}

void runtime::context::unlink_and_strip(node& target)
{
    // Unlink first so nothing walks into the subtree once it is gone from the
    // scene, then unwind the components (renderer registrations, lights,
    // cameras) of the whole subtree.
    if (node* parent = target.parent())
    {
        parent->remove(target);
    }
    release_subtree_components(target);
}

void runtime::context::collect_doomed(node& target)
{
    for (node* child : target.m_children)
    {
        // A descendant with a destroy request of its own (queued, or already
        // collected) is left to it.
        if (!child->m_destroy_pending)
        {
            collect_doomed(*child);
        }
    }
    // Children first, so each is freed while its parent is still there to
    // detach from. A caller-owned node is not ours to free; its parent's
    // destruction orphans it.
    if (target.m_owning_scene != nullptr)
    {
        target.m_destroy_pending = true;
        m_doomed.push_back(&target);
    }
}

void runtime::context::free_doomed()
{
    std::vector<node*> doomed;
    doomed.swap(m_doomed);
    for (node* target : doomed)
    {
        // Normally this scene; a node another scene owns that sat in this
        // subtree goes back to its own pool.
        target->m_owning_scene->free_node(*target);
    }
}

void runtime::context::free_node(node& target)
{
    m_nodes.release(target.m_pool_slot);
}

runtime::node& runtime::context::clone(node& source, node* parent)
{
    node& copy = create_node(source.m_name, parent);
    copy_pose(source, copy);

    // The copy's components go into the store it is scoped to right now; if
    // that scene is mid-walk its pools must not grow, so the rest waits for
    // the end of its update.
    context* copy_scene = copy.scene();
    if (copy_scene != nullptr && copy_scene->is_traversing())
    {
        copy_scene->defer([this, &source, &copy] { clone_contents(source, copy); });
    }
    else
    {
        clone_contents(source, copy);
    }
    return copy;
}

void runtime::context::clone_contents(node& source, node& copy)
{
    // Plan the whole subtree before creating anything. The copy may sit
    // inside the source subtree (a node cloned under its own child), and the
    // plan must not walk into the nodes it is about to make.
    struct planned
    {
        node* source;
        std::size_t parent;
    };
    std::vector<planned> plan{planned{&source, 0}};
    for (std::size_t i = 0; i < plan.size(); ++i)
    {
        for (node* child : plan[i].source->m_children)
        {
            if (child != &copy)
            {
                plan.push_back(planned{child, i});
            }
        }
    }

    // Breadth-first, so every parent copy exists before its children and
    // siblings keep their order.
    std::vector<node*> copies(plan.size(), nullptr);
    for (std::size_t i = 0; i < plan.size(); ++i)
    {
        node& from = *plan[i].source;
        node* to = &copy;
        if (i != 0)
        {
            to = &create_node(from.m_name, copies[plan[i].parent]);
            copy_pose(from, *to);
        }
        copies[i] = to;
        to->copy_components_from(from);
        // After the components, so a disabled copy hides them through the
        // usual on_active_changed.
        to->set_active(from.m_active);
    }
}

std::size_t runtime::context::node_count() const noexcept
{
    return m_nodes.size();
}

runtime::node* runtime::context::find(core::string_id name)
{
    if (name.empty())
    {
        return nullptr;
    }
    auto it = m_name_index.find(name);
    return it != m_name_index.end() && !it->second.empty() ? it->second.front() : nullptr;
}

void runtime::context::index_name(node& target)
{
    if (!target.m_name.empty())
    {
        m_name_index[target.m_name].push_back(&target);
    }
}

void runtime::context::unindex_name(node& target)
{
    if (target.m_name.empty())
    {
        return;
    }
    auto it = m_name_index.find(target.m_name);
    if (it == m_name_index.end())
    {
        return;
    }
    std::vector<node*>& named = it->second;
    named.erase(std::remove(named.begin(), named.end(), &target), named.end());
    if (named.empty())
    {
        m_name_index.erase(it);
    }
}

void runtime::context::release_all()
{
    // Everything the scene can reach: the tree under the root, then each
    // scene-owned node outside it (detached, or re-parented into another
    // scene) with its subtree. Each subtree is listed in pre-order, so
    // walking the list backwards mostly meets a node after its descendants;
    // where it does not, freeing the parent first only orphans them.
    std::vector<node*> order;
    std::unordered_set<const node*> seen;
    std::vector<node*> stack;
    auto gather = [&](node& top)
    {
        stack.push_back(&top);
        while (!stack.empty())
        {
            node* current = stack.back();
            stack.pop_back();
            if (!seen.insert(current).second)
            {
                continue;
            }
            order.push_back(current);
            for (auto it = current->m_children.rbegin(); it != current->m_children.rend(); ++it)
            {
                stack.push_back(*it);
            }
        }
    };
    gather(root);
    for (node* owned : m_nodes.live())
    {
        gather(*owned);
    }

    std::size_t cut_loose = 0;
    for (auto it = order.rbegin(); it != order.rend(); ++it)
    {
        node& target = **it;
        if (&target == &root)
        {
            continue;
        }
        if (target.m_owning_scene == this)
        {
            // Frees its components (on_destroy) and detaches it; anything
            // still below it has already been freed or cut loose.
            free_node(target);
            continue;
        }
        if (target.m_store != &components)
        {
            continue;
        }

        // Not ours to free (caller-owned, or another scene's node linked in
        // here): free its components while the store is alive and drop the
        // store, so destroying it later never reaches into this scene. The
        // links below it stay intact for its owner.
        target.release_components();
        unindex_name(target);
        target.m_store = nullptr;
        if (target.m_parent == &root)
        {
            ++cut_loose;
            root.remove(target);
        }
    }
    m_doomed.clear();

    if (cut_loose > 0)
    {
        LOG_WRN("runtime::context: %zu caller-owned subtree(s) still attached at teardown; freed their components",
                cut_loose);
    }
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
        LOG_WRN("runtime::context::defer_destroy: '%s' is already pending destruction; ignoring",
                target.m_name.c_str());
        return;
    }
    target.m_destroy_pending = true;

    defer(
        [this, &target, release = std::move(release)]
        {
            unlink_and_strip(target);
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
        // The retired nodes stay allocated (detached, component-less) until a
        // drain completes: a command left over may still name one.
        LOG_ERR(
            "runtime::context::apply_deferred: commands kept re-queueing for %d rounds; %zu left for the next update",
            k_max_drain_rounds,
            m_pending.size());
        return;
    }
    free_doomed();
}

std::size_t runtime::context::pending_command_count() const noexcept
{
    return m_pending.size();
}

bool runtime::context::is_traversing() const noexcept
{
    return m_traversal_depth > 0;
}
