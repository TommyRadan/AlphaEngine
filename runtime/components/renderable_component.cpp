// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/renderable_component.hpp>

#include <rendering_engine/renderer.hpp>
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
