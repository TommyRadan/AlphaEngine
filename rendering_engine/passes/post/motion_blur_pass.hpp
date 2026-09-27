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

namespace rendering_engine
{
    /**
     * @brief Camera motion blur in HDR, from the velocity buffer.
     *
     * A single fullscreen-triangle stage: every pixel averages the scene
     * colour along its own motion vector from @ref velocity_pass, scaled by
     * the shutter (@ref motion_blur_settings::intensity), clamped to
     * @ref motion_blur_settings::max_radius pixels and sampled
     * symmetrically about the pixel (see
     * @c shaders/passes/motion_blur.frag.glsl). The motion vectors cover
     * camera motion over the static world, so that is what blurs.
     *
     * It sits after @ref volumetric_fog_pass and before @ref bloom_pass:
     * after the fog so the haze smears with the scene it hangs over, in
     * HDR so a bright highlight streaks with its full energy instead of a
     * clipped one, and before bloom and @ref auto_exposure_pass so the glow
     * spreads from, and the exposure meters, the image actually shown. That
     * also puts it ahead of @ref taa_pass, which in this engine resolves
     * the tonemapped LDR image: the resolve then reprojects and clamps the
     * already blurred frame, and the blur averages the frame's sub-pixel
     * jitter away along the motion anyway.
     *
     * A blur cannot sample the image it writes, so the pass draws into a
     * full-resolution @c rgba16f target of its own rather than back into
     * the scene colour. @ref renderer::render asks @ref draws before any
     * pass records and, when it does, publishes that target as
     * @ref frame_context::hdr_color_target / @c hdr_color_texture, which
     * bloom, auto exposure and tonemap read instead of the scene colour.
     * When it does not (disabled — the default — or no motion vectors),
     * @ref record returns at once and they read the scene colour: the
     * previous stage, at no cost. In the declared pass I/O both are the logical
     * "scene_color", which this pass reads and writes like bloom does.
     *
     * The output target is only allocated the first time motion blur is
     * switched on (@ref prepare, called by @ref renderer::render outside the
     * frame), so the default configuration does not pay for a
     * full-resolution target it never draws; once allocated it stays for
     * the pass's lifetime. The inputs arrive through the frame context and
     * the bind group is rebuilt whenever either handle differs from the one
     * it was built against; @ref resize recreates an allocated output
     * target (new before old, so the published handle changes for its
     * consumers). The params UBO is rewritten every frame the pass draws,
     * inside the frame bracket. A degenerate backbuffer leaves the pass
     * disabled.
     */
    struct motion_blur_pass : pass
    {
        // @p width / @p height size the output target.
        motion_blur_pass(uint32_t width, uint32_t height);
        ~motion_blur_pass() override;

        motion_blur_pass(const motion_blur_pass&) = delete;
        motion_blur_pass& operator=(const motion_blur_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "motion_blur";
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read("scene_color");
            io.read("velocity");
            io.write("scene_color");
        }

        // Notes the new drawable size and, once the output target exists,
        // recreates it at that size (new before old, so bloom, auto
        // exposure and tonemap see a different handle and rebind); the new
        // size reaches the params UBO on the next drawn record(). The bind
        // group samples only the inputs, whose new handles the frame
        // context republishes, so it needs no work here. No-op while the
        // pass is disabled.
        void resize(uint32_t width, uint32_t height) override;

        /**
         * @brief Allocates the output target the first time @p settings
         *        make the pass active (@ref motion_blur_active). Called by
         *        @ref renderer::render ahead of each frame, outside the
         *        frame bracket; a no-op once the target exists or while
         *        motion blur stays off.
         */
        void prepare(const motion_blur_settings& settings);

        /**
         * @brief Whether @ref record draws for @p ctx: the pass is live and
         *        its target allocated, @ref frame_context::post enables
         *        motion blur (@ref motion_blur_active) and motion vectors
         *        are published. @ref renderer::render decides
         *        @ref frame_context::hdr_color_target by it.
         */
        bool draws(const frame_context& ctx) const;

        /// The blurred HDR image's target and texture (see the class
        /// comment); invalid until @ref prepare allocates them, and they
        /// change on @ref resize.
        gpu::render_target output_target() const;
        gpu::texture output_texture() const;

    private:
        // Allocates the rgba16f output target at @ref m_width x
        // @ref m_height.
        void create_target();

        // Rebuilds the bind group against @p scene_color and @p velocity
        // plus the params UBO, remembering both handles.
        void rebuild_bind_group(gpu::texture scene_color, gpu::texture velocity);

        // Packs this frame's params block (the settings, the noise frame
        // and the target size) into @ref m_params_ubo.
        void upload_params(const frame_context& ctx);

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_params_ubo{};

        // {scene colour @0, velocity @1, params @2}.
        gpu::bind_group_layout m_layout{};
        gpu::pipeline m_pipeline{};

        gpu::render_target m_target{};
        gpu::texture m_texture{};
        gpu::bind_group m_bind_group{};

        // The inputs @ref m_bind_group was built against; invalid until
        // the first drawn frame builds it.
        gpu::texture m_bound_color{};
        gpu::texture m_bound_velocity{};

        // The drawable size the output target is (or will be) allocated
        // at, and the size the params block describes.
        uint32_t m_width{0};
        uint32_t m_height{0};

        // False when the backbuffer dimensions are degenerate (no
        // settings, zero-sized window); record() then no-ops.
        bool m_enabled{false};
    };
} // namespace rendering_engine
