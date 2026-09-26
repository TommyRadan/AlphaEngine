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
#include <rendering_engine/render_graph/frame_graph.hpp>

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
     * handle stable from frame to frame; it only changes on @ref resize,
     * which the FXAA pass detects through the handle comparison it makes
     * on every frame.
     *
     * Pipeline state mirrors every other fullscreen-triangle post pass
     * (depth off, blend off, no culling, single vec2 vertex attribute).
     * The reciprocal frame size the neighbourhood taps step by is baked
     * from the backbuffer dimensions at construction, mirroring
     * @ref fxaa_pass, and rewritten by @ref resize, which also recreates
     * both targets and drops the history so the first frame at the new
     * size does not reproject a stale, differently sized image. A
     * degenerate backbuffer leaves the pass disabled so the LDR target
     * flows straight through to FXAA.
     */
    struct taa_pass : pass
    {
        // @p width / @p height are the backbuffer dimensions the history
        // and resolve targets are sized against. The two textures the
        // resolve samples are not constructor inputs: the tonemapped LDR
        // image and the motion vectors arrive every frame as
        // @ref frame_context::ldr_color_texture and
        // @ref frame_context::velocity_texture, and the resolve bind group
        // is (re)built whenever either handle differs from the one it was
        // last built against.
        taa_pass(uint32_t width, uint32_t height);
        ~taa_pass() override;

        taa_pass(const taa_pass&) = delete;
        taa_pass& operator=(const taa_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "taa";
        }

        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.read("ldr_color");
            io.read("velocity");
            io.read("taa_history");
            io.write("taa_resolve");
            io.write("taa_history");
        }

        // Recreates the history and resolve targets at the new drawable
        // size, notes the new texel step for the resolve params UBO (the
        // next record() rewrites it, inside the frame bracket) and drops
        // the history (the next frame resolves from the current image
        // alone, exactly like the first frame after construction). The
        // new targets are created before the old ones are released so the
        // handle published through frame_context::taa_resolve_texture
        // changes and FXAA rebinds. No-op while the pass is disabled.
        void resize(uint32_t width, uint32_t height) override;

        // The resolved LDR texture the next pass (FXAA) samples. The
        // engine publishes it every frame as
        // @ref frame_context::taa_resolve_texture; it changes on
        // @ref resize. Invalid when the pass is disabled (degenerate
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

        // The LDR and velocity textures @ref m_resolve_bind_group was built
        // against; invalid until the first record() builds the group, and
        // reset by resize() so the group is rebuilt against the new
        // history target.
        gpu::texture m_bound_current{};
        gpu::texture m_bound_velocity{};

        // Allocates the rgba8 history and resolve targets at
        // @p width x @p height and points the four target / texture
        // members at them.
        void create_targets(uint32_t width, uint32_t height);

        // Rebuilds the resolve bind group against @p current_color and
        // @p velocity plus the history texture and params UBO, remembering
        // the two input handles.
        void rebuild_resolve_bind_group(gpu::texture current_color, gpu::texture velocity);

        // Rebuilds the history-store copy bind group against the current
        // resolve texture.
        void rebuild_copy_bind_group();

        // Writes {1/width, 1/height, feedback, 0} to the resolve params UBO
        // and remembers the feedback in @ref m_uploaded_feedback. Only
        // called from record(), inside the frame bracket: the buffer is
        // host-mapped on a deferred-execution backend and the previous
        // frame may still be reading it until begin_frame waits.
        void write_params(float feedback);

        // Per-texel step (1/width, 1/height) baked at construction and
        // rewritten by resize(). Kept so record() can rewrite the resolve
        // params UBO — bumping only the feedback weight — without losing
        // the step in xy.
        float m_inv_width{0.0f};
        float m_inv_height{0.0f};

        // The feedback weight the params UBO currently holds, or negative
        // when the UBO must be rewritten whatever the weight (the texel
        // step changed: construction, resize). record() compares the
        // weight the frame needs against it and rewrites on mismatch.
        float m_uploaded_feedback{-1.0f};

        // True until the first frame has populated the history target.
        // While set, the resolve uses the current frame only (the history
        // is still undefined); the frame after switches the params UBO to
        // the steady-state feedback weight. resize() sets it again.
        bool m_first_frame{true};

        // False when the backbuffer dimensions are degenerate (no settings,
        // zero-sized window); record() then no-ops and output_texture()
        // returns an invalid handle so the caller keeps the tonemap output.
        bool m_enabled{false};
    };
} // namespace rendering_engine
