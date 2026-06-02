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
     * @brief Fast approximate anti-aliasing on the tonemapped LDR image.
     *
     * The cheapest post-process AA: a single fullscreen pass that finds
     * luminance edges in the already-shaded LDR image and blends along
     * them, smoothing the jaggies the scene pass leaves behind without
     * any MSAA sample storage. It is the last link in the post chain,
     * running after @ref tonemap_pass on the LDR intermediate target and
     * writing the result straight to the swapchain the @ref ui_pass then
     * composites on top of.
     *
     * FXAA wants a perceptual (non-linear) image and computes its edge
     * luma from the colour directly, so it slots in *after* tonemap's
     * ACES + gamma encode rather than on the linear HDR target — sampling
     * the gamma-encoded LDR is exactly the input Timothy Lottes' original
     * shader assumes. The implementation is the canonical compact FXAA:
     * a 3x3 luma neighbourhood picks the edge direction, then two pairs
     * of bilinear taps along that direction produce the blended colour,
     * falling back to the tighter blend when the wider one drifts outside
     * the local luma range.
     *
     * Pipeline state mirrors every other fullscreen-triangle post pass
     * (depth off, blend off, no culling, single vec2 vertex attribute);
     * the shared @ref fullscreen_triangle_vertex_shader emits the
     * geometry. The reciprocal frame size (1/width, 1/height) the edge
     * search steps by is baked into the input bind group at construction
     * from the backbuffer dimensions, mirroring the way
     * @ref tonemap_pass captures its exposure; live resolution changes
     * are out of scope. A degenerate backbuffer bakes a zero step, which
     * collapses every tap onto the centre texel so the pass becomes a
     * straight copy.
     */
    struct fxaa_pass : pass
    {
        // @p input_color is the LDR texture @ref tonemap_pass renders
        // into; @p width / @p height are the backbuffer dimensions the
        // per-texel edge step is baked from.
        fxaa_pass(gpu::texture input_color, uint32_t width, uint32_t height);
        ~fxaa_pass() override;

        fxaa_pass(const fxaa_pass&) = delete;
        fxaa_pass& operator=(const fxaa_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

    private:
        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_rcp_frame_ubo{};
        gpu::bind_group_layout m_input_layout{};
        gpu::bind_group m_input_bind_group{};
        gpu::pipeline m_pipeline{};
    };
} // namespace rendering_engine
