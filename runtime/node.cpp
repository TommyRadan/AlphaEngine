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

#include <runtime/node.hpp>

#include <algorithm>
#include <cassert>

#include <runtime/scene_graph.hpp>

runtime::node::node()
    : m_parent{nullptr}, m_store{nullptr}, m_active{true}, m_effective_active{true}, m_destroy_pending{false}
{
}

runtime::node::~node()
{
    // A node dying mid-traversal is the one mutation that cannot be deferred:
    // the parent's child list is being walked. Nothing to do but say so.
    reject_during_traversal("~node");

    // Free this node's components before anything else; the store outlives the
    // node, so leaving handles dangling would leak pooled slots.
    release_components();

    // Detach from the parent so its child list never references freed memory.
    detach_from_parent();

    // Orphan the children back to world space; they outlive this node and must
    // not keep pointing at it. As roots they answer to their own active flag.
    std::vector<node*> children;
    children.swap(m_children);
    for (node* child : children)
    {
        child->m_parent = nullptr;
        child->transform.set_parent(nullptr);
        child->refresh_active(true);
    }
}

void runtime::node::add(node& child)
{
    if (child.m_parent == this)
    {
        return;
    }

    // Linking an ancestor (or this node) below itself would close a cycle and
    // send every world-matrix walk into an infinite loop.
    for (const node* ancestor = this; ancestor != nullptr; ancestor = ancestor->m_parent)
    {
        if (ancestor == &child)
        {
            LOG_ERR("runtime::node::add: '%s' is '%s' or one of its ancestors; refusing to create a cycle",
                    child.name.c_str(),
                    name.c_str());
            return;
        }
    }

    if (reject_during_traversal("add"))
    {
        defer([this, &child] { add(child); });
        return;
    }

    // A node lives under a single parent at a time; pull it off any previous
    // one before re-linking. The active state is settled once, below, against
    // the new parent rather than bouncing through world space first.
    child.detach_from_parent();

    child.m_parent = this;
    child.transform.set_parent(&transform);
    // The subtree draws its components from this scene's store. A child that
    // already carries components in another store has them moved across.
    if (m_store != nullptr && child.m_store != m_store)
    {
        child.set_store(m_store);
    }
    m_children.push_back(&child);

    // The child subtree inherits this node's effective-active state, so a node
    // moved under a disabled parent hides (and shows again under an enabled one).
    child.refresh_active(m_effective_active);
}

void runtime::node::remove(node& child)
{
    auto it = std::find(m_children.begin(), m_children.end(), &child);
    if (it == m_children.end())
    {
        return;
    }

    if (reject_during_traversal("remove"))
    {
        defer([this, &child] { remove(child); });
        return;
    }

    child.detach_from_parent();
    // Back in world space the child is a root: effectively active whenever its
    // own flag is, no matter what the parent it just left was.
    child.refresh_active(true);
}

void runtime::node::detach_from_parent()
{
    if (m_parent == nullptr)
    {
        return;
    }
    auto it = std::find(m_parent->m_children.begin(), m_parent->m_children.end(), this);
    if (it != m_parent->m_children.end())
    {
        m_parent->m_children.erase(it);
    }
    m_parent = nullptr;
    transform.set_parent(nullptr);
}

runtime::node* runtime::node::parent() const noexcept
{
    return m_parent;
}

const std::vector<runtime::node*>& runtime::node::children() const noexcept
{
    return m_children;
}

core::math::mat4 runtime::node::world_matrix() const
{
    return transform.get_world_matrix();
}

void runtime::node::update_subtree()
{
    // A disabled node — or one under a disabled ancestor — freezes its whole
    // subtree: no component updates run.
    if (!m_effective_active)
    {
        return;
    }

    // on_update hooks may not restructure the lists being walked here; the
    // scope makes the immediate APIs assert (debug) or defer (release).
    context::traversal_scope traversal{scene()};

    if (m_store != nullptr)
    {
        for (const component_entry& entry : m_components)
        {
            m_store->update(entry.type, entry.handle, *this);
        }
    }

    for (node* child : m_children)
    {
        child->update_subtree();
    }
}

void runtime::node::set_store(component_store* store)
{
    if (store != m_store)
    {
        if (m_store != nullptr && !m_components.empty())
        {
            if (store == nullptr)
            {
                // Components cannot exist without a pool to live in.
                release_components();
            }
            else
            {
                for (component_entry& entry : m_components)
                {
                    entry.handle = m_store->migrate(entry.type, entry.handle, *store);
                }
            }
        }
        m_store = store;
    }

    // The whole subtree lives in one scene: hand the store down, migrating
    // any descendant that was scoped elsewhere.
    for (node* child : m_children)
    {
        child->set_store(store);
    }
}

runtime::component_store* runtime::node::store() const noexcept
{
    return m_store;
}

runtime::context* runtime::node::scene() const noexcept
{
    return m_store != nullptr ? m_store->scene() : nullptr;
}

bool runtime::node::is_destroy_pending() const noexcept
{
    return m_destroy_pending;
}

void runtime::node::remove_all_components()
{
    if (m_components.empty())
    {
        return;
    }
    if (reject_during_traversal("remove_all_components"))
    {
        defer([this] { remove_all_components(); });
        return;
    }
    release_components();
}

void runtime::node::release_components()
{
    if (m_store != nullptr)
    {
        for (const component_entry& entry : m_components)
        {
            m_store->erase(entry.type, entry.handle);
        }
    }
    m_components.clear();
}

runtime::node* runtime::node::find(const std::string& target)
{
    if (name == target)
    {
        return this;
    }
    for (node* child : m_children)
    {
        if (node* found = child->find(target))
        {
            return found;
        }
    }
    return nullptr;
}

core::math::vec3 runtime::node::world_position() const
{
    const core::math::mat4 world = world_matrix();
    return core::math::vec3{world.m[12], world.m[13], world.m[14]};
}

void runtime::node::set_world_position(const core::math::vec3& world_position)
{
    // local = inverse(parent_world) * world_position (as a point).
    if (m_parent == nullptr)
    {
        transform.set_position(world_position);
        return;
    }
    const core::math::mat4 parent_inverse = core::math::inverse(m_parent->world_matrix());
    const core::math::vec4 local = parent_inverse * core::math::vec4{world_position, 1.0f};
    transform.set_position(core::math::vec3{local.x, local.y, local.z});
}

void runtime::node::look_at(const core::math::vec3& target, const core::math::vec3& up)
{
    // Re-express the world-space target in the node's local frame, then defer to
    // the transform's local look_at. Exact when ancestors are unrotated.
    if (m_parent == nullptr)
    {
        transform.look_at(target, up);
        return;
    }
    const core::math::mat4 parent_inverse = core::math::inverse(m_parent->world_matrix());
    const core::math::vec4 local_target = parent_inverse * core::math::vec4{target, 1.0f};
    transform.look_at(core::math::vec3{local_target.x, local_target.y, local_target.z}, up);
}

bool runtime::node::is_active() const noexcept
{
    return m_active;
}

bool runtime::node::is_effective_active() const noexcept
{
    return m_effective_active;
}

void runtime::node::set_active(bool active)
{
    if (active == m_active)
    {
        return;
    }
    if (reject_during_traversal("set_active"))
    {
        defer([this, active] { set_active(active); });
        return;
    }
    m_active = active;
    const bool parent_effective = m_parent != nullptr ? m_parent->m_effective_active : true;
    refresh_active(parent_effective);
}

void runtime::node::refresh_active(bool parent_effective)
{
    const bool effective = parent_effective && m_active;
    if (effective == m_effective_active)
    {
        // No change here means no change downstream either.
        return;
    }
    m_effective_active = effective;

    // on_active_changed hooks run against the lists walked below, so they are
    // held to the same no-structural-mutation rule as on_update.
    context::traversal_scope traversal{scene()};

    if (m_store != nullptr)
    {
        for (const component_entry& entry : m_components)
        {
            m_store->set_active(entry.type, entry.handle, *this, effective);
        }
    }

    for (node* child : m_children)
    {
        child->refresh_active(effective);
    }
}

bool runtime::node::reject_during_traversal(const char* operation) const
{
    const context* owner = scene();
    if (owner == nullptr || !owner->is_traversing())
    {
        return false;
    }
    LOG_ERR("runtime::node::%s on '%s': called from inside a scene traversal (on_update / on_active_changed). "
            "The call is deferred to the end of context::update; use context::defer_* to make that explicit",
            operation,
            name.c_str());
    assert(false && "runtime::node: structural mutation from inside a scene traversal");
    return true;
}

void runtime::node::defer(std::function<void()> command)
{
    context* owner = scene();
    if (owner == nullptr)
    {
        LOG_ERR("runtime::node::defer on '%s': node belongs to no scene; command dropped", name.c_str());
        return;
    }
    owner->defer(std::move(command));
}
