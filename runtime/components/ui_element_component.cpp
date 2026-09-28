// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/components/ui_element_component.hpp>

#include <core/log.hpp>
#include <rendering_engine/render_world.hpp>
#include <runtime/node.hpp>
#include <runtime/scene.hpp>

void runtime::ui_element_component::on_attach(node& owner)
{
    if (!m_element)
    {
        return;
    }
    runtime::scene* scene = owner.scene();
    m_world = scene != nullptr ? scene->world() : nullptr;
    if (m_world == nullptr)
    {
        LOG_WRN("runtime::ui_element_component::on_attach: node has no scene render_world; the element has no proxy");
        return;
    }
    create_proxy();
}

void runtime::ui_element_component::on_destroy()
{
    destroy_proxy();
    m_world = nullptr;
}

void runtime::ui_element_component::on_active_changed(node& owner, bool active)
{
    (void)owner;
    if (active)
    {
        create_proxy();
    }
    else
    {
        destroy_proxy();
    }
}

void runtime::ui_element_component::extract(const node& owner)
{
    (void)owner;
    if (m_world == nullptr || !m_element || !m_proxy.valid())
    {
        return;
    }
    const uint64_t revision = m_element->revision();
    if (revision != m_captured_revision)
    {
        m_world->set_ui_element(m_proxy, m_element->capture());
        m_captured_revision = revision;
    }
}

void runtime::ui_element_component::create_proxy()
{
    if (m_world == nullptr || !m_element || m_proxy.valid())
    {
        return;
    }
    m_captured_revision = m_element->revision();
    m_proxy = m_world->create_ui_element(m_element->capture());
}

void runtime::ui_element_component::destroy_proxy()
{
    if (m_world != nullptr && m_proxy.valid())
    {
        m_world->destroy_ui_element(m_proxy);
    }
    m_proxy = {};
}
