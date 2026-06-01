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

#include <scene_graph/node.hpp>

#include <algorithm>

scene_graph::node::node() : m_parent{nullptr}, m_store{nullptr}, m_active{true}, m_effective_active{true} {}

scene_graph::node::~node()
{
    // Free this node's components before anything else; the store outlives the
    // node, so leaving handles dangling would leak pooled slots.
    if (m_store != nullptr)
    {
        for (const component_entry& entry : m_components)
        {
            m_store->erase(entry.type, entry.handle);
        }
    }
    m_components.clear();

    // Detach from the parent so its child list never references freed memory.
    if (m_parent != nullptr)
    {
        m_parent->remove(*this);
    }

    // Orphan the children back to world space; they outlive this node and must
    // not keep pointing at it.
    for (node* child : m_children)
    {
        child->m_parent = nullptr;
        child->transform.set_parent(nullptr);
    }
    m_children.clear();
}

void scene_graph::node::add(node& child)
{
    if (&child == this || child.m_parent == this)
    {
        return;
    }

    // A node lives under a single parent at a time; pull it off any previous
    // one before re-linking.
    if (child.m_parent != nullptr)
    {
        child.m_parent->remove(child);
    }

    child.m_parent = this;
    child.transform.set_parent(&transform);
    if (child.m_store == nullptr)
    {
        child.set_store(m_store);
    }
    m_children.push_back(&child);

    // The child subtree inherits this node's effective-active state, so a node
    // moved under a disabled parent hides (and shows again under an enabled one).
    child.refresh_active(m_effective_active);
}

void scene_graph::node::remove(node& child)
{
    auto it = std::find(m_children.begin(), m_children.end(), &child);
    if (it == m_children.end())
    {
        return;
    }

    m_children.erase(it);
    child.m_parent = nullptr;
    child.transform.set_parent(nullptr);
}

scene_graph::node* scene_graph::node::parent() const noexcept
{
    return m_parent;
}

const std::vector<scene_graph::node*>& scene_graph::node::children() const noexcept
{
    return m_children;
}

infrastructure::math::mat4 scene_graph::node::world_matrix() const
{
    return transform.get_world_matrix();
}

void scene_graph::node::update_subtree()
{
    // A disabled node freezes its whole subtree — no component updates run.
    if (!m_active)
    {
        return;
    }

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

void scene_graph::node::set_store(component_store* store) noexcept
{
    m_store = store;
    // Hand the store down to any descendants that do not have one yet so a
    // whole subtree built before being parented into a scene picks it up.
    for (node* child : m_children)
    {
        if (child->m_store == nullptr)
        {
            child->set_store(store);
        }
    }
}

scene_graph::component_store* scene_graph::node::store() const noexcept
{
    return m_store;
}

scene_graph::node* scene_graph::node::find(const std::string& target)
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

infrastructure::math::vec3 scene_graph::node::world_position() const
{
    const infrastructure::math::mat4 world = world_matrix();
    return infrastructure::math::vec3{world.m[12], world.m[13], world.m[14]};
}

void scene_graph::node::set_world_position(const infrastructure::math::vec3& world_position)
{
    // local = inverse(parent_world) * world_position (as a point).
    if (m_parent == nullptr)
    {
        transform.set_position(world_position);
        return;
    }
    const infrastructure::math::mat4 parent_inverse = infrastructure::math::inverse(m_parent->world_matrix());
    const infrastructure::math::vec4 local = parent_inverse * infrastructure::math::vec4{world_position, 1.0f};
    transform.set_position(infrastructure::math::vec3{local.x, local.y, local.z});
}

void scene_graph::node::look_at(const infrastructure::math::vec3& target, const infrastructure::math::vec3& up)
{
    // Re-express the world-space target in the node's local frame, then defer to
    // the transform's local look_at. Exact when ancestors are unrotated.
    if (m_parent == nullptr)
    {
        transform.look_at(target, up);
        return;
    }
    const infrastructure::math::mat4 parent_inverse = infrastructure::math::inverse(m_parent->world_matrix());
    const infrastructure::math::vec4 local_target = parent_inverse * infrastructure::math::vec4{target, 1.0f};
    transform.look_at(infrastructure::math::vec3{local_target.x, local_target.y, local_target.z}, up);
}

bool scene_graph::node::is_active() const noexcept
{
    return m_active;
}

bool scene_graph::node::is_effective_active() const noexcept
{
    return m_effective_active;
}

void scene_graph::node::set_active(bool active)
{
    if (active == m_active)
    {
        return;
    }
    m_active = active;
    const bool parent_effective = m_parent != nullptr ? m_parent->m_effective_active : true;
    refresh_active(parent_effective);
}

void scene_graph::node::refresh_active(bool parent_effective)
{
    const bool effective = parent_effective && m_active;
    if (effective == m_effective_active)
    {
        // No change here means no change downstream either.
        return;
    }
    m_effective_active = effective;

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
