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

#pragma once

#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>

namespace rendering_engine
{
    struct scene_pass;

    /**
     * @brief Optional depth pre-pass ahead of the @ref scene_pass.
     *
     * Opt-in (@ref frame_context::depth_prepass, from
     * @c core::settings::graphics.depth_prepass and
     * @ref context::set_depth_prepass). While it is off, or no camera is
     * active, the pass records nothing and the scene pass clears and
     * writes the scene depth itself as it always has; it stays in the
     * frame graph either way so the setting can flip at runtime.
     *
     * When it runs it lays the opaque queue's depth into the scene
     * target's depth attachment, through a depth-only render target that
     * imports that attachment (the depth-only targets the shadow passes
     * use), before any colour is shaded. The draw list is the scene
     * pass's own (@ref scene_pass::prepare): layer-filtered and
     * frustum-culled against the camera, sorted by
     * @ref draw_item::sort_key, so the opaque items it draws go
     * front-to-back; the transparent queue and every surface
     * @ref material::draws_in_depth_prepass rejects (blended, not
     * depth-tested or not depth-writing, or a template whose fragment
     * stage decides coverage) are skipped. The scene pass then loads the
     * depth and draws each pre-passed surface with depth writes off and a
     * less-or-equal test (@ref material::depth_prepassed_pipeline), so a
     * covered pixel is shaded once, while everything the pre-pass skipped
     * keeps its ordinary less-and-write variant against the same buffer.
     *
     * Both passes must produce bit-identical depth for a surface. The
     * pre-pass therefore draws each item through its material's own
     * pipeline variant minus the fragment stage
     * (@ref material::depth_prepass_pipeline): the same vertex module
     * (skinning included), vertex layout and rasterizer state, the same
     * per-frame group with the TAA-jittered view-projection, the same
     * per-draw block at the same dynamic offset, into a target of the
     * same extent; and the vertex shaders declare @c gl_Position
     * @c invariant so the two pipelines may not compile the position
     * differently.
     *
     * The frame graph sees it write @c scene_depth, which the scene pass
     * reads (loads) and writes after it. Later depth consumers get the
     * same depth either way.
     */
    struct depth_prepass : pass
    {
        // @p scene is the scene pass that runs right after this one and
        // owns the draw list, the per-frame bind group and the choice
        // between loading and clearing the depth. Non-owning; the engine
        // context owns both passes.
        explicit depth_prepass(scene_pass* scene);
        ~depth_prepass() override;

        depth_prepass(const depth_prepass&) = delete;
        depth_prepass& operator=(const depth_prepass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "depth_prepass";
        }

        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.write("scene_depth");
        }

        // Drops the depth-only target: it imports the scene target's
        // depth attachment, which the context has just recreated at the
        // new size, so the next @ref record rebuilds it over the new one.
        void resize(uint32_t width, uint32_t height) override;

    private:
        void release_target();

        // Non-owning; see the constructor.
        scene_pass* m_scene{nullptr};

        // Depth-only target over @ref m_target_depth, the scene depth
        // attachment it was built against. Rebuilt whenever
        // @ref frame_context::scene_depth_texture publishes another
        // handle; the attachment itself stays owned by the context's
        // scene-colour target.
        gpu::render_target m_target{};
        gpu::texture m_target_depth{};
    };
} // namespace rendering_engine
