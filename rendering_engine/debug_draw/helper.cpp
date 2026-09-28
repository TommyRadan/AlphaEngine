// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/helper.hpp>

#include <rendering_engine/renderer.hpp>

namespace rendering_engine::debug_draw
{
    helper::helper(renderer& owner, const char* name) : m_renderer(&owner), m_name(name)
    {
        m_renderer->world().add_helper(*this);
    }

    helper::~helper()
    {
        m_renderer->world().remove_helper(*this);
    }

    const char* helper::name() const noexcept
    {
        return m_name;
    }

    bool helper::is_visible() const noexcept
    {
        return m_visible;
    }

    void helper::set_visible(bool visible)
    {
        m_visible = visible;
    }

    renderer& helper::owner() const noexcept
    {
        return *m_renderer;
    }

    const render_world& helper::renderer_world() const noexcept
    {
        return m_renderer->world();
    }

    core::math::vec3 helper::to_rgb(const assets::color& c)
    {
        return core::math::vec3{
            static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f, static_cast<float>(c.b) / 255.0f};
    }
} // namespace rendering_engine::debug_draw
