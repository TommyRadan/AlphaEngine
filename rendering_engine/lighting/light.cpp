// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/lighting/light.hpp>

#include <rendering_engine/render_world.hpp>

rendering_engine::light::light(light_type type) : m_type(type) {}

rendering_engine::light::~light()
{
    detach();
}

rendering_engine::light_type rendering_engine::light::type() const noexcept
{
    return m_type;
}

void rendering_engine::light::attach(render_world& world)
{
    detach();
    m_world = &world;
    if (m_enabled)
    {
        m_world->add_light(*this);
    }
}

void rendering_engine::light::detach()
{
    if (m_world == nullptr)
    {
        return;
    }
    // A no-op for a light that was disabled at the time.
    m_world->remove_light(*this);
    m_world = nullptr;
}

bool rendering_engine::light::is_attached() const noexcept
{
    return m_world != nullptr;
}

void rendering_engine::light::set_enabled(bool enabled)
{
    if (enabled == m_enabled)
    {
        return;
    }
    m_enabled = enabled;
    if (m_world == nullptr)
    {
        return;
    }
    if (enabled)
    {
        m_world->add_light(*this);
    }
    else
    {
        m_world->remove_light(*this);
    }
}

bool rendering_engine::light::is_enabled() const noexcept
{
    return m_enabled;
}
