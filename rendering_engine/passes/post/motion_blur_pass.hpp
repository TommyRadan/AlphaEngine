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
     * Each view gets an output target at its size, a params UBO and a bind
     * group of its own (see @ref view_data), allocated the first time
     * motion blur is switched on for the view, so the default
     * configuration does not pay for a full-resolution target it never
     * draws; once allocated they stay as long as the view does, the target
     * recreated when the view's size changes (new before old, so the
     * published handle changes for its consumers). The inputs are looked
     * up every frame and the bind group is rebuilt whenever either handle
     * differs from the one it was built against. The params UBO is
     * rewritten every frame the pass draws, inside the frame bracket.
     */
    struct motion_blur_pass : pass
    {
        explicit motion_blur_pass(gpu::device& device);
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

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // What the pass keeps per view: the rgba16f output target at the
        // view's size (@ref width x @ref height, which the params block
        // describes), the params UBO, and the bind group over the inputs
        // it was built against (invalid until the view's first drawn frame
        // builds it) and that UBO.
        struct view_data final : pass_view_state
        {
            explicit view_data(gpu::device& device);
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            // (Re)allocates the output target at @p width x @p height, the
            // new one before the old one is released.
            void resize(uint32_t width, uint32_t height);

            gpu::device* device{nullptr};
            gpu::buffer params_ubo{};
            gpu::render_target target{};
            gpu::texture texture{};
            gpu::bind_group bind_group{};
            gpu::texture bound_color{};
            gpu::texture bound_velocity{};
            uint32_t width{0};
            uint32_t height{0};
        };

        // Rebuilds @p view's bind group against @p scene_color and
        // @p velocity plus its params UBO, remembering both handles.
        void rebuild_bind_group(view_data& view, gpu::texture scene_color, gpu::texture velocity);

        // Packs this frame's params block (the settings, the noise frame
        // and the view's size) into @p view's params UBO.
        void upload_params(const frame_context& ctx, const view_data& view);

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::buffer m_vertex_buffer{};

        // {scene colour @0, velocity @1, params @2}.
        gpu::bind_group_layout m_layout{};
        gpu::pipeline m_pipeline{};

        // The view's output target and bind group this frame's record()
        // draws with, looked up by prepare().
        gpu::render_target m_target{};
        gpu::bind_group m_bind_group{};

        // Whether this frame's record() draws, decided by prepare().
        bool m_draws{false};
    };
} // namespace rendering_engine
