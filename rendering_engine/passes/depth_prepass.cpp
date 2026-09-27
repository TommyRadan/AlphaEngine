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

#include <rendering_engine/passes/depth_prepass.hpp>

#include <core/log.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/passes/scene_pass.hpp>
#include <runtime/engine.hpp>

namespace rendering_engine
{
    depth_prepass::depth_prepass(scene_pass* scene) : m_scene(scene) {}

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
        // the context's scene-colour target.
        if (m_target.valid())
        {
            runtime::current_engine().gpu->destroy(m_target);
        }
        m_target = {};
        m_target_depth = {};
    }

    void depth_prepass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        // Off, or nothing to draw the scene with: record nothing at all.
        // The scene pass sees that no pre-pass ran this frame and clears
        // the depth itself, exactly as it does with the pre-pass disabled.
        if (!ctx.depth_prepass || ctx.active_camera == nullptr || m_scene == nullptr)
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

        // The culling, sorting and per-frame uploads are the scene pass's.
        // Run them now: the vertex stages below read this frame's
        // view_globals block (and OpenGL executes each draw as it is
        // recorded), and the pre-pass must draw the very items, per-draw
        // blocks and order the scene pass will shade.
        m_scene->prepare(ctx);

        // Clear to the far plane and keep the result for the scene pass,
        // which loads it instead of clearing.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_target;
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::clear;
        descriptor.depth.store = gpu::store_op::store;
        descriptor.depth.clear_depth = 1.0f;

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        m_scene->record_depth_prepass(*pass_encoder);
        pass_encoder->end();
    }
} // namespace rendering_engine
