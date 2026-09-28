// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
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
     * the scene colour. On a frame it draws, @ref prepare republishes
     * @ref frame_resources::scene_color with that target, so bloom, auto
     * exposure and tonemap, which prepare after it, read the blurred copy
     * instead of the scene colour, while the passes before it keep the
     * original. When it does not draw (disabled — the default — or no
     * motion vectors), it publishes nothing, @ref record returns at once
     * and they read the scene colour: the previous stage, at no cost.
     *
     * The output target is only allocated the first time motion blur is
     * switched on, so the default configuration does not pay for a
     * full-resolution target it never draws; once allocated it stays for
     * the pass's lifetime. The inputs are looked up every frame and the
     * bind group is rebuilt whenever either handle differs from the one it
     * was built against; @ref resize recreates an allocated output target
     * (new before old, so the published handle changes for its consumers).
     * The params UBO is rewritten every frame the pass draws, inside the
     * frame bracket. A degenerate backbuffer leaves the pass disabled.
     */
    struct motion_blur_pass : pass
    {
        // @p width / @p height size the output target.
        motion_blur_pass(gpu::device& device, uint32_t width, uint32_t height);
        ~motion_blur_pass() override;

        motion_blur_pass(const motion_blur_pass&) = delete;
        motion_blur_pass& operator=(const motion_blur_pass&) = delete;

        // Allocates the output target the first time motion blur is on,
        // decides whether the frame blurs — the pass is live, the settings
        // enable it (@ref motion_blur_active) and motion vectors are
        // published — and, when it does, writes the params block, rebinds
        // the inputs when their handles changed and republishes the scene
        // colour with its output.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::motion_blur;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::scene_color);
            io.read_optional(frame_resources::velocity);
            io.write(frame_resources::scene_color);
        }

        // Notes the new drawable size and, once the output target exists,
        // recreates it at that size (new before old, so bloom, auto
        // exposure and tonemap see a different handle and rebind); the new
        // size reaches the params UBO on the next drawn prepare(). The bind
        // group samples only the inputs, whose new handles their owners
        // publish, so it needs no work here. No-op while the pass is
        // disabled.
        void resize(uint32_t width, uint32_t height) override;

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

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

        // Whether this frame's record() draws, decided by prepare().
        bool m_draws{false};
    };
} // namespace rendering_engine
