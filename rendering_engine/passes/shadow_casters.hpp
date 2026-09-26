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

/**
 * @file shadow_casters.hpp
 * @brief The instanced depth-only pipeline and the caster dispatch shared
 *        by the directional and omni shadow passes.
 */

#pragma once

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine::gpu
{
    struct render_pass_encoder;
}

namespace rendering_engine
{
    /**
     * @brief The instanced twin of a shadow pass's depth-only pipeline.
     *
     * The single-draw shadow pipeline takes each caster's model matrix from
     * the per-draw UBO its renderable binds at slot 1. An @ref instanced_mesh
     * has no such group: its transforms travel in the per-instance vertex
     * stream it binds at vertex slot 1 (four vec4 model columns, then a
     * tint; see @ref instanced_material) and it draws indexed-indirect. This
     * pipeline reads the same stream through
     * @c shaders/passes/shadow_instanced.vert.glsl, so instanced batches
     * cast with the same light view-projection and fixed-function state as
     * every other caster.
     */
    struct instanced_shadow_pipeline
    {
        gpu::shader_module vertex_shader{};
        gpu::pipeline pipeline{};
    };

    // Builds the instanced pipeline over @p fragment_shader (the shared
    // depth-only stage), @p light_layout at slot 0 and the given depth /
    // blend / rasterizer state, which the caller passes unchanged from its
    // single-draw pipeline so both rasterize identically.
    instanced_shadow_pipeline create_instanced_shadow_pipeline(gpu::shader_module fragment_shader,
                                                               gpu::bind_group_layout light_layout,
                                                               const gpu::depth_state& depth,
                                                               const gpu::blend_state& blend,
                                                               const gpu::rasterizer_state& rasterizer);

    // Releases the pipeline and its vertex shader; no-op for invalid handles.
    void destroy_instanced_shadow_pipeline(instanced_shadow_pipeline& instanced);

    /**
     * @brief Records caster draw items into an open shadow render pass.
     *
     * Picks the single-draw or the instanced pipeline per item (an item
     * with an indirect command is an instanced batch), binding the light
     * bind group at slot 0 again whenever the pipeline changes, and issues
     * the matching draw. Items that carry neither a per-draw group nor an
     * instance stream have no model matrix to cast with and are skipped.
     */
    class shadow_caster_dispatch
    {
    public:
        shadow_caster_dispatch(gpu::render_pass_encoder& encoder,
                               gpu::pipeline single_pipeline,
                               gpu::pipeline instanced_pipeline,
                               gpu::bind_group light_bind_group);

        void draw(const draw_item& item);

    private:
        gpu::render_pass_encoder& m_encoder;
        gpu::pipeline m_single_pipeline{};
        gpu::pipeline m_instanced_pipeline{};
        gpu::bind_group m_light_bind_group{};

        // The pipeline currently set on the encoder; invalid until the
        // first draw.
        gpu::pipeline m_bound_pipeline{};
    };
} // namespace rendering_engine
