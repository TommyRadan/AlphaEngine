// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/debug_draw/helper.hpp>

#include <rendering_engine/renderer.hpp>

namespace rendering_engine::debug_draw
{
    helper::helper(renderer& owner, const char* name, helper_layer layer)
        : m_renderer(&owner), m_name(name), m_layer(layer)
    {
        m_renderer->world().add_helper(*this);

        // Debug gizmos are editor-only geometry: they stay on the default
        // camera mask (layer_all includes layer_editor) so nothing changes
        // visually, but a game can build a camera that clears the editor
        // bit to hide them from gameplay views.
        layer_mask = layer_editor;

        if (m_layer == helper_layer::scene)
        {
            m_renderer->register_scene_renderable(this);
        }
        else
        {
            m_renderer->register_debug_renderable(this);
        }
    }

    helper::~helper()
    {
        if (m_layer == helper_layer::scene)
        {
            m_renderer->unregister_scene_renderable(this);
        }
        else
        {
            m_renderer->unregister_debug_renderable(this);
        }

        m_renderer->world().remove_helper(*this);
    }

    const char* helper::name() const noexcept
    {
        return m_name;
    }

    core::math::vec3 helper::to_rgb(const assets::color& c)
    {
        return core::math::vec3{
            static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f, static_cast<float>(c.b) / 255.0f};
    }
} // namespace rendering_engine::debug_draw
