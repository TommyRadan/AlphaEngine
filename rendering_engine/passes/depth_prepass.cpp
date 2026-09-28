// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/depth_prepass.hpp>

#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>

namespace rendering_engine
{
    depth_prepass::depth_prepass(gpu::device& device) : m_device(&device) {}

    depth_prepass::~depth_prepass()
    {
        release_target();
    }

    void depth_prepass::resize(uint32_t /*width*/, uint32_t /*height*/)
    {
        release_target();
    }

    void depth_prepass::release_target()
    {
        // Only the target goes: the depth attachment it imports belongs to
        // the renderer's scene-colour target.
        if (m_target.valid())
        {
            m_device->destroy(m_target);
        }
        m_target = {};
        m_target_depth = {};
    }

    void depth_prepass::prepare(const frame_context& ctx)
    {
        m_active = false;

        // Off, or nothing to draw the scene with: record nothing at all.
        // The scene pass, told nothing, clears the depth itself, exactly
        // as it does with the pre-pass disabled.
        if (!ctx.depth_prepass || ctx.active_camera == nullptr)
        {
            return;
        }

        // Follow the scene depth attachment: a resize recreates it, and
        // the handle comparison catches any other swap too.
        const gpu::texture scene_depth = ctx.resources->get(frame_resources::scene_depth);
        if (m_target.valid() && m_target_depth != scene_depth)
        {
            release_target();
        }
        if (!m_target.valid())
        {
            if (!scene_depth.valid())
            {
                return;
            }
            gpu::render_target_descriptor descriptor = gpu::render_target_descriptor::depth_only(
                gpu::texture_format::depth24, ctx.viewport_width, ctx.viewport_height);
            descriptor.depth.texture = scene_depth;
            m_target = m_device->create_render_target(descriptor);
            if (!m_target.valid())
            {
                LOG_ERR("depth_prepass: could not build a depth-only target over the scene depth; the scene pass "
                        "clears and writes depth itself");
                return;
            }
            m_target_depth = scene_depth;
        }

        // The culling, sorting and per-frame uploads are the scene pass's,
        // which prepares right after this one: finding the pre-pass's
        // target published, it resolves each pre-passed item's depth-only
        // twin, and its record() loads the depth this pass leaves instead
        // of clearing it. The pre-pass then draws the very items, per-draw
        // blocks and order the scene pass will shade.
        m_active = true;
        ctx.resources->publish(frame_resources::depth_prepass, m_target);
    }

    void depth_prepass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        // The scene pass's list, published while it prepared after this
        // pass; without a scene pass there is nothing to lay down.
        const scene_view_data* view = m_active ? ctx.resources->find(frame_resources::scene_view) : nullptr;
        if (view == nullptr || view->depth_prepass == nullptr)
        {
            return;
        }

        // Clear to the far plane and keep the result for the scene pass,
        // which loads it instead of clearing. The scene pass begins the
        // pass and dispatches its list — from worker threads above the
        // parallel draw threshold, as for the shading pass.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_target;
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::clear;
        descriptor.depth.store = gpu::store_op::store;
        descriptor.depth.clear_depth = 1.0f;
        view->depth_prepass->record_depth_prepass(encoder, descriptor);
    }
} // namespace rendering_engine
