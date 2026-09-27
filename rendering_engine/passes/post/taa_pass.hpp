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
#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    struct camera;

    /**
     * @brief Temporal anti-aliasing resolve on the tonemapped LDR image.
     *
     * The temporal partner to @ref fxaa_pass: where FXAA smooths a single
     * frame spatially, TAA accumulates many sub-pixel-jittered frames into
     * one stable, supersampled image. The scene pass jitters the
     * projection matrix by the Halton(2,3) offset the renderer publishes in
     * @ref frame_context::jitter each frame (gated on the same
     * @c temporal_aa setting), so every frame samples the scene at a
     * slightly different sub-pixel position; blending those frames over
     * time resolves detail that no single-frame filter can — fine geometry
     * edges, specular shimmer, the near-mirror IBL reflections — which is
     * exactly the THREE.TAARenderPass / SSAARenderPass behaviour this
     * mirrors.
     *
     * The resolve runs after @ref tonemap_pass on the LDR target (TAA on
     * the perceptual image keeps HDR fireflies from dominating the history)
     * and feeds @ref fxaa_pass, which closes the post chain. Each frame the
     * resolve stage reprojects the colour history along the per-pixel
     * motion vectors from @ref velocity_pass — sampling the history at
     * @c texCoord - velocity rather than the same pixel — so a moving
     * camera keeps the accumulated detail aligned to the surface instead of
     * smearing it. A 3x3 neighbourhood colour clamp then constrains that
     * reprojected history to the current frame's local min/max box before
     * the blend, suppressing the ghosting that reprojection alone leaves at
     * disocclusions; history that reprojects off-screen is dropped in
     * favour of the current frame.
     *
     * The history is a ping-pong pair of @c rgba8 targets: the frame's
     * resolve writes into one while sampling the other (last frame's
     * resolve) as history, and the roles swap after every frame, so no
     * copy is needed to store the history. The target being written this
     * frame is exposed via @ref output_texture so the next pass (FXAA)
     * samples it instead of the raw tonemap output; the handle therefore
     * alternates between two values from frame to frame (and changes
     * altogether on @ref resize), which FXAA absorbs with a small
     * per-handle bind-group cache.
     *
     * The history is only meaningful for frames of the same camera, so the
     * resolve restarts from the current image alone (history weight 0)
     * on the first frame, whenever the frame's camera differs from the one
     * the history was accumulated from, on a no-camera frame, and after a
     * resize — a camera switch or teleport-by-reattach never blends against
     * a stale image. The reciprocal frame size the neighbourhood taps step
     * by is baked from the backbuffer dimensions at construction, mirroring
     * @ref fxaa_pass, and rewritten by @ref resize, which also recreates
     * both targets. Pipeline state mirrors every other fullscreen-triangle
     * post pass (depth off, blend off, no culling, single vec2 vertex
     * attribute). A degenerate backbuffer leaves the pass disabled so the
     * LDR target flows straight through to FXAA.
     */
    struct taa_pass : pass
    {
        // @p width / @p height are the backbuffer dimensions the two
        // accumulation targets are sized against. The two textures the
        // resolve samples are not constructor inputs: the tonemapped LDR
        // image and the motion vectors arrive every frame as
        // @ref frame_context::ldr_color_texture and
        // @ref frame_context::velocity_texture, and the resolve bind groups
        // are (re)built whenever either handle differs from the one they
        // were last built against.
        taa_pass(uint32_t width, uint32_t height);
        ~taa_pass() override;

        taa_pass(const taa_pass&) = delete;
        taa_pass& operator=(const taa_pass&) = delete;

        // Restarts the history on a camera change, rebinds the inputs when
        // their handles changed, writes the feedback weight the frame
        // needs, picks the half this frame resolves into and swaps the
        // pair's roles for the next frame.
        void prepare(const frame_context& ctx) override;

        // Draws the resolve into the half @ref prepare picked.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "taa";
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read("ldr_color");
            io.read("velocity");
            io.read("taa_history");
            io.write("taa_resolve");
            io.write("taa_history");
        }

        // Recreates both accumulation targets at the new drawable size,
        // notes the new texel step for the resolve params UBO (the next
        // record() rewrites it, inside the frame bracket) and drops the
        // history (the next frame resolves from the current image alone,
        // exactly like the first frame after construction). The new
        // targets are created before the old ones are released so the
        // handle published through frame_context::taa_resolve_texture
        // changes and FXAA rebinds. No-op while the pass is disabled.
        void resize(uint32_t width, uint32_t height) override;

        // The resolved LDR texture the next pass (FXAA) samples: the
        // target the coming frame's resolve writes (the renderer asks
        // before the passes prepare). The engine publishes it every frame
        // as @ref frame_context::taa_resolve_texture; it alternates
        // between the two ping-pong targets from frame to frame and
        // changes on @ref resize. Invalid when the pass is disabled
        // (degenerate backbuffer), in which case the caller should keep
        // sampling the raw tonemap output.
        gpu::texture output_texture() const;

    private:
        // One half of the ping-pong pair: the target, its colour texture,
        // and the resolve bind group that writes into it while sampling the
        // *other* half as history ({currentColor @0, historyColor @1,
        // velocity @2, params @3}).
        struct accumulation_target
        {
            gpu::render_target target{};
            gpu::texture texture{};
            gpu::bind_group resolve_bind_group{};
        };

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_resolve_shader{};

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_resolve_ubo{};

        gpu::bind_group_layout m_resolve_layout{};
        gpu::pipeline m_resolve_pipeline{};

        // The pair, which half the next frame's resolve writes into (the
        // other half then holds the history) and which half this frame's
        // record() draws into, taken from the former by prepare(), which
        // swaps the roles afterwards. Both own their colour attachment, so
        // destroying the target releases the texture.
        std::array<accumulation_target, 2> m_targets{};
        uint32_t m_write_index{0};
        uint32_t m_draw_index{0};

        // The LDR and velocity textures the resolve bind groups were built
        // against; invalid until the first record() builds them, and reset
        // by resize() so they are rebuilt against the new targets.
        gpu::texture m_bound_current{};
        gpu::texture m_bound_velocity{};

        // The camera the history was accumulated from (null before the
        // first frame and across no-camera frames); a frame whose camera
        // differs restarts the accumulation.
        const camera* m_history_camera{nullptr};

        // Allocates both rgba8 accumulation targets at @p width x @p height.
        void create_targets(uint32_t width, uint32_t height);

        // Rebuilds both resolve bind groups against @p current_color and
        // @p velocity plus each half's opposite texture as history and the
        // params UBO, remembering the two input handles.
        void rebuild_resolve_bind_groups(gpu::texture current_color, gpu::texture velocity);

        // Releases both resolve bind groups (no-op for invalid handles).
        void destroy_resolve_bind_groups();

        // Writes {1/width, 1/height, feedback, 0} to the resolve params UBO
        // and remembers the feedback in @ref m_uploaded_feedback. Only
        // called from prepare(), inside the frame bracket: the buffer is
        // host-mapped on a deferred-execution backend and the previous
        // frame may still be reading it until begin_frame waits.
        void write_params(float feedback);

        // Per-texel step (1/width, 1/height) baked at construction and
        // rewritten by resize(). Kept so prepare() can rewrite the resolve
        // params UBO — bumping only the feedback weight — without losing
        // the step in xy.
        float m_inv_width{0.0f};
        float m_inv_height{0.0f};

        // The feedback weight the params UBO currently holds, or negative
        // when the UBO must be rewritten whatever the weight (the texel
        // step changed: construction, resize). prepare() compares the
        // weight the frame needs against it and rewrites on mismatch.
        float m_uploaded_feedback{-1.0f};

        // True while the history is unusable: before the first frame,
        // after a resize, and whenever the camera changed or went away.
        // While set, the resolve uses the current frame only; the frame
        // after switches the params UBO to the steady-state feedback.
        bool m_first_frame{true};

        // False when the backbuffer dimensions are degenerate (no settings,
        // zero-sized window); record() then no-ops and output_texture()
        // returns an invalid handle so the caller keeps the tonemap output.
        bool m_enabled{false};
    };
} // namespace rendering_engine
