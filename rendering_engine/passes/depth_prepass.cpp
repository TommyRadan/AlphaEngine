// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/depth_prepass.hpp>

#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/passes/scene_pass.hpp>
#include <runtime/engine.hpp>

namespace rendering_engine
{
    depth_prepass::depth_prepass() = default;

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
            runtime::current_engine().gpu->destroy(m_target);
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
        if (!ctx.depth_prepass || ctx.active_camera == nullptr || ctx.scene == nullptr)
        {
            return;
        }

        // Follow the scene depth attachment: a resize recreates it, and
        // the handle comparison catches any other swap too.
        if (m_target.valid() && m_target_depth != ctx.scene_depth_texture)
        {
            release_target();
        }
        if (!m_target.valid())
        {
            if (!ctx.scene_depth_texture.valid())
            {
                return;
            }
            gpu::render_target_descriptor descriptor = gpu::render_target_descriptor::depth_only(
                gpu::texture_format::depth24, ctx.viewport_width, ctx.viewport_height);
            descriptor.depth.texture = ctx.scene_depth_texture;
            m_target = runtime::current_engine().gpu->create_render_target(descriptor);
            if (!m_target.valid())
            {
                LOG_ERR("depth_prepass: could not build a depth-only target over the scene depth; the scene pass "
                        "clears and writes depth itself");
                return;
            }
            m_target_depth = ctx.scene_depth_texture;
        }

        // The culling, sorting and per-frame uploads are the scene pass's,
        // which prepares right after this one: told that the pre-pass runs,
        // it resolves each pre-passed item's depth-only twin, and its
        // record() loads the depth this pass leaves instead of clearing it.
        // The pre-pass then draws the very items, per-draw blocks and order
        // the scene pass will shade.
        m_active = true;
        ctx.scene->expect_depth_prepass();
    }

    void depth_prepass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        if (!m_active || ctx.scene == nullptr)
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
        ctx.scene->record_depth_prepass(encoder, descriptor);
    }
} // namespace rendering_engine
