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

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Temporal anti-aliasing resolve on the tonemapped LDR image.
     *
     * The temporal partner to @ref fxaa_pass: where FXAA smooths a single
     * frame spatially, TAA accumulates many sub-pixel-jittered frames into
     * one stable, supersampled image. The scene pass jitters the
     * projection matrix by a Halton(2,3) offset each frame (gated on the
     * same @c temporal_aa setting), so every frame samples the scene at a
     * slightly different sub-pixel position; blending those frames over
     * time resolves detail that no single-frame filter can — fine geometry
     * edges, specular shimmer, the near-mirror IBL reflections — which is
     * exactly the THREE.TAARenderPass / SSAARenderPass behaviour this
     * mirrors.
     *
     * The resolve runs after @ref tonemap_pass on the LDR target (TAA on
     * the perceptual image keeps HDR fireflies from dominating the history)
     * and feeds @ref fxaa_pass, which closes the post chain. Each frame:
     *
     *  1. The resolve stage reprojects the colour history along the
     *     per-pixel motion vectors from @ref velocity_pass — sampling the
     *     history at @c texCoord - velocity rather than the same pixel — so
     *     a moving camera keeps the accumulated detail aligned to the
     *     surface instead of smearing it. A 3x3 neighbourhood colour clamp
     *     then constrains that reprojected history to the current frame's
     *     local min/max box before the blend, suppressing the ghosting that
     *     reprojection alone leaves at disocclusions; history that
     *     reprojects off-screen is dropped in favour of the current frame.
     *  2. A copy stage stores the resolved frame into the history target
     *     for the next frame to read.
     *
     * The resolved result is exposed via @ref output_texture so the next
     * pass (FXAA) samples it instead of the raw tonemap output. A fixed
     * resolve target (rather than ping-ponged history handles) keeps that
     * handle stable across frames so the FXAA bind group never has to be
     * rebuilt.
     *
     * Pipeline state mirrors every other fullscreen-triangle post pass
     * (depth off, blend off, no culling, single vec2 vertex attribute).
     * The reciprocal frame size the neighbourhood taps step by is baked
     * from the backbuffer dimensions at construction, mirroring
     * @ref fxaa_pass; live resolution changes are out of scope. A
     * degenerate backbuffer leaves the pass disabled so the LDR target
     * flows straight through to FXAA.
     */
    struct taa_pass : pass
    {
        // @p current_color is the LDR texture @ref tonemap_pass renders
        // into; @p velocity is the motion-vector texture @ref velocity_pass
        // produces, sampled to reproject the history; @p width / @p height
        // are the backbuffer dimensions the history and resolve targets are
        // sized against.
        taa_pass(gpu::texture current_color, gpu::texture velocity, uint32_t width, uint32_t height);
        ~taa_pass() override;

        taa_pass(const taa_pass&) = delete;
        taa_pass& operator=(const taa_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        // The resolved LDR texture the next pass (FXAA) samples. Stable
        // across frames. Invalid when the pass is disabled (degenerate
        // backbuffer), in which case the caller should keep sampling the
        // raw tonemap output.
        gpu::texture output_texture() const;

    private:
        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_resolve_shader{};
        gpu::shader_module m_copy_shader{};

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_resolve_ubo{};

        // {currentColor @0, historyColor @1, velocity @2, params @3} for
        // the resolve stage; {src @0} for the history-store copy.
        gpu::bind_group_layout m_resolve_layout{};
        gpu::bind_group_layout m_copy_layout{};

        gpu::pipeline m_resolve_pipeline{};
        gpu::pipeline m_copy_pipeline{};

        // History holds the previous frame's resolved image; resolve holds
        // this frame's. The copy stage stores resolve into history at the
        // end of the frame. Both own their colour attachment, so destroying
        // the target releases the texture.
        gpu::render_target m_history_target{};
        gpu::texture m_history_texture{};
        gpu::render_target m_resolve_target{};
        gpu::texture m_resolve_texture{};

        gpu::bind_group m_resolve_bind_group{};
        gpu::bind_group m_copy_bind_group{};

        // Per-texel step (1/width, 1/height) baked at construction. Kept so
        // record() can rewrite the resolve params UBO — bumping only the
        // feedback weight — without losing the step in xy.
        float m_inv_width{0.0f};
        float m_inv_height{0.0f};

        // False until the first frame has populated the history target.
        // While set, the resolve uses the current frame only (the history
        // is still undefined), then the params UBO is switched to the
        // steady-state feedback weight.
        bool m_first_frame{true};

        // False when the backbuffer dimensions are degenerate (no settings,
        // zero-sized window); record() then no-ops and output_texture()
        // returns an invalid handle so the caller keeps the tonemap output.
        bool m_enabled{false};
    };
} // namespace rendering_engine
