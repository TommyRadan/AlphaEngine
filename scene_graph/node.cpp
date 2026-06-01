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

scene_graph::node::node() : m_parent{nullptr} {}

scene_graph::node::~node()
{
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
    m_children.push_back(&child);
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
