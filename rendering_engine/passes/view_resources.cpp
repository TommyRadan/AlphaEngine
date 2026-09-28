// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/view_resources.hpp>

#include <algorithm>
#include <cassert>

#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>

namespace rendering_engine
{
    view_resources::view_resources(gpu::device& device, camera_proxy_handle camera)
        : m_device{&device}, m_camera{camera}
    {
    }

    view_resources::~view_resources()
    {
        // The passes' state first: its bind groups sample the targets below.
        m_states.clear();
        release_targets();
    }

    bool view_resources::resize(uint32_t width, uint32_t height)
    {
        if (width == m_width && height == m_height && m_scene_color.target.valid())
        {
            return false;
        }

        // The HDR scene-colour target the scene pass renders into: rgba16f
        // rather than the output's format, so tonemap, bloom and every other
        // post effect sample real HDR luminance.
        gpu::render_target_descriptor scene_color_descriptor{};
        scene_color_descriptor.color = {{gpu::texture_format::rgba16_float}};
        scene_color_descriptor.width = width;
        scene_color_descriptor.height = height;
        scene_color_descriptor.with_depth = true;
        scene_color_descriptor.depth.format = gpu::texture_format::depth24;

        // The LDR target tonemap resolves into and the anti-aliasing passes
        // sample, since the view's output (the swapchain, say) cannot be
        // bound as a shader input. No depth: the post chain runs
        // depth-disabled.
        gpu::render_target_descriptor ldr_color_descriptor{};
        ldr_color_descriptor.color = {{gpu::texture_format::rgba8_unorm}};
        ldr_color_descriptor.width = width;
        ldr_color_descriptor.height = height;
        ldr_color_descriptor.with_depth = false;

        // New targets first, so every handle a pass compares against
        // changes; the device defers freeing the old ones until the last
        // command buffer that used them has retired.
        const color_target old_scene_color = m_scene_color;
        const color_target old_ldr_color = m_ldr_color;
        m_scene_color.target = m_device->create_render_target(scene_color_descriptor);
        m_scene_color.texture = m_device->render_target_color_texture(m_scene_color.target);
        m_scene_depth = m_device->render_target_depth_texture(m_scene_color.target);
        m_ldr_color.target = m_device->create_render_target(ldr_color_descriptor);
        m_ldr_color.texture = m_device->render_target_color_texture(m_ldr_color.target);
        if (old_ldr_color.target.valid())
        {
            m_device->destroy(old_ldr_color.target);
        }
        if (old_scene_color.target.valid())
        {
            m_device->destroy(old_scene_color.target);
        }

        LOG_INF("Rendering Engine: view %u:%u targets %s %ux%u",
                m_camera.index,
                m_camera.generation,
                m_width == 0 ? "created at" : "resized to",
                width,
                height);
        m_width = width;
        m_height = height;
        // What the store holds names the released targets.
        m_resources.clear();
        return true;
    }

    void view_resources::drop_state(const pass& owner)
    {
        m_states.erase(
            std::remove_if(m_states.begin(), m_states.end(), [&owner](const entry& e) { return e.owner == &owner; }),
            m_states.end());
    }

    pass_view_state* view_resources::find(const pass& owner, const std::type_info& type) const
    {
        for (const entry& e : m_states)
        {
            if (e.owner == &owner)
            {
                assert(e.type == std::type_index{type} && "a pass keeps one type of view state");
                return e.type == std::type_index{type} ? e.state.get() : nullptr;
            }
        }
        return nullptr;
    }

    void view_resources::release_targets()
    {
        // Each target owns its attachments, so destroying it releases them.
        if (m_ldr_color.target.valid())
        {
            m_device->destroy(m_ldr_color.target);
        }
        if (m_scene_color.target.valid())
        {
            m_device->destroy(m_scene_color.target);
        }
        m_ldr_color = {};
        m_scene_color = {};
        m_scene_depth = {};
        m_width = 0;
        m_height = 0;
    }
} // namespace rendering_engine
