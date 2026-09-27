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

#include <runtime/components/renderable_component.hpp>

#include <rendering_engine/rendering_engine.hpp>
#include <runtime/engine.hpp>
#include <runtime/node.hpp>

void runtime::renderable_component::on_attach(node& owner)
{
    if (!m_renderable)
    {
        return;
    }

    // Draw at the node's world pose: the renderable's own transform becomes
    // a local offset under the node through the transform parent chain.
    if (m_transform != nullptr)
    {
        m_transform->set_parent(&owner.transform);
    }
    register_renderable();
}

void runtime::renderable_component::on_destroy()
{
    unregister_renderable();
    // The owning node may outlive this component (remove_component, store
    // teardown); do not leave the transform pointing at it.
    if (m_renderable && m_transform != nullptr)
    {
        m_transform->set_parent(nullptr);
    }
}

void runtime::renderable_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (active)
    {
        register_renderable();
    }
    else
    {
        unregister_renderable();
    }
}

void runtime::renderable_component::register_renderable()
{
    if (m_renderable && !m_registered)
    {
        runtime::current_engine().renderer->register_scene_renderable(m_renderable.get());
        m_registered = true;
    }
}

void runtime::renderable_component::unregister_renderable()
{
    if (m_renderable && m_registered)
    {
        runtime::current_engine().renderer->unregister_scene_renderable(m_renderable.get());
        m_registered = false;
    }
}
