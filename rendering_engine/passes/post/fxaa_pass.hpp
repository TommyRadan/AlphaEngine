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

#include <array>
#include <cstddef>
#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>

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
     * search steps by is baked into a small UBO at construction from the
     * backbuffer dimensions, mirroring the way @ref tonemap_pass
     * captures its exposure, and rewritten by @ref resize. A degenerate
     * backbuffer bakes a zero step, which collapses every tap onto the
     * centre texel so the pass becomes a straight copy.
     *
     * The image it samples is not a constructor input: each frame it
     * takes @ref frame_context::taa_resolve_texture when that is valid
     * (temporal AA on) and @ref frame_context::ldr_color_texture
     * otherwise, and binds an input bind group built against that handle.
     * The TAA resolve alternates between two ping-pong targets from frame
     * to frame, so the groups are kept in a two-entry cache keyed by
     * handle: a handle seen before is rebound without work, a new one (the
     * first frame, a resize that recreated a target) replaces the entry
     * it displaces — which is how a resize reaches this pass.
     */
    struct fxaa_pass : pass
    {
        // @p width / @p height are the backbuffer dimensions the per-texel
        // edge step is baked from. @p taa_enabled says whether the context
        // publishes a TAA resolve for this pass to sample (see
        // @ref declare_io); the per-frame choice still follows the handle's
        // validity.
        fxaa_pass(uint32_t width, uint32_t height, bool taa_enabled);
        ~fxaa_pass() override;

        fxaa_pass(const fxaa_pass&) = delete;
        fxaa_pass& operator=(const fxaa_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "fxaa";
        }

        // Declares the input the engine actually wires: the TAA resolve
        // when temporal AA is on, else the tonemapped LDR target.
        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.read(m_taa_enabled ? "taa_resolve" : "ldr_color");
            io.write("swapchain");
        }

        // Notes the new drawable size for the per-texel edge step; the next
        // record() rewrites the rcp_frame UBO with it, inside the frame
        // bracket. The input bind group needs no work here: it is rebound
        // by record() when the sampled handle changes.
        void resize(uint32_t width, uint32_t height) override;

    private:
        // One cached input: the texture and the bind group built against
        // it plus the rcp_frame UBO.
        struct bound_input
        {
            gpu::texture texture{};
            gpu::bind_group bind_group{};
        };

        // The bind group for @p input_color: the cached one when the handle
        // was bound before, otherwise a new group built into the slot the
        // cache rotates to (releasing whatever it held).
        gpu::bind_group bind_group_for(gpu::texture input_color);

        // Writes {1/width, 1/height, 0, 0} to the rcp_frame UBO; a zero
        // dimension writes a zero step. Only called from record(), after
        // begin_frame has waited for the previous frame that may still
        // read the buffer.
        void write_rcp_frame(uint32_t width, uint32_t height);

        // The drawable size resize() last reported, and whether the UBO
        // still has to be rewritten with it.
        uint32_t m_pending_width{0};
        uint32_t m_pending_height{0};
        bool m_rcp_frame_dirty{false};

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_rcp_frame_ubo{};
        gpu::bind_group_layout m_input_layout{};
        gpu::pipeline m_pipeline{};

        // Two cached inputs — the two halves of the TAA ping-pong, or just
        // the LDR target — and the slot the next miss is built into.
        // Entries are invalid until the first record() builds one.
        std::array<bound_input, 2> m_inputs{};
        size_t m_next_input_slot{0};

        // Whether the engine wires the TAA resolve as this pass's input.
        bool m_taa_enabled{false};
    };
} // namespace rendering_engine
