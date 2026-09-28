// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>

namespace rendering_engine
{
    /**
     * @brief Raymarched volumetric fog: lit haze and light shafts
     *        composited over the HDR scene colour before bloom and
     *        tonemap.
     *
     * The distance and height fog the lit materials apply are analytic
     * per-fragment terms: they cannot show light scattering through the
     * air, occluders cutting shafts out of it, or fog glowing around a
     * point or spot light. This pass marches the height-fog medium per
     * pixel instead, in three fullscreen-triangle stages:
     *
     *  1. March (half resolution): reconstructs the pixel's world position
     *     from the scene depth and the jittered inverse view-projection it
     *     was rasterised with, then steps from the camera toward it (capped
     *     at @ref volumetric_fog_settings::max_distance), accumulating the
     *     transmittance and the light the medium scatters toward the
     *     camera: ambient, every directional light through a
     *     Henyey-Greenstein phase and a tap of the cascaded shadow map, and
     *     the first few point and spot lights with the lit materials'
     *     attenuation and a tap of their shadow maps. The start of every
     *     ray is jittered by interleaved gradient noise, varied per frame
     *     while temporal AA is on so the resolve averages the banding away.
     *  2. Upsample (full resolution): a depth-aware (joint bilateral)
     *     upsample of the march into a full-resolution target, so fog does
     *     not bleed across silhouettes.
     *  3. Composite: blends the upsampled fog into the scene-colour target
     *     as scene * transmittance + in-scattered light (blend factors one
     *     / src_alpha, rgb only).
     *
     * The composite blends rather than sampling the scene colour, so it
     * needs no ping-pong copy.
     *
     * The march binds the scene pass's per-frame group at slot 0 (the
     * @ref view_globals block, the lights and the shadow blocks and maps),
     * handed in at construction the way the debug pass takes it, plus its
     * own parameters and the scene depth at slot 1. The medium is the
     * scene's height fog (@ref fog_settings::height_density, falloff and
     * reference height) times @ref volumetric_fog_settings::density_scale;
     * while it runs the lit materials start their analytic height fog at
     * the march's max distance (see @ref volumetric_fog_active), so the
     * two never attenuate the same stretch twice.
     *
     * Always in the pass list, but @ref record draws nothing unless
     * @ref frame_context::post enables it, a camera is active and the
     * height fog has a density, so with the default settings the frame
     * is unchanged. A degenerate backbuffer leaves the pass disabled.
     */
    struct volumetric_fog_pass : pass
    {
        // @p frame_layout is the scene pass's per-frame layout, which the
        // march pipeline reserves slot 0 for. @p width / @p height are the
        // drawable size the targets follow. The group bound there is not a
        // constructor input: the march binds the scene pass's jittered
        // per-frame group (the view the scene depth was rasterised with),
        // read through @ref frame_context::scene each frame after the scene
        // pass refilled its buffers, so it always reflects the current
        // view, lights and shadow casters. Nor is the scene depth: it
        // arrives every frame as @ref frame_context::scene_depth_texture,
        // and the bind groups sampling it are rebuilt whenever that handle
        // changes.
        volumetric_fog_pass(gpu::device& device, gpu::bind_group_layout frame_layout, uint32_t width, uint32_t height);
        ~volumetric_fog_pass() override;

        volumetric_fog_pass(const volumetric_fog_pass&) = delete;
        volumetric_fog_pass& operator=(const volumetric_fog_pass&) = delete;

        // Decides whether the frame marches, writes the params block and
        // rebinds the scene depth when its handle changed.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "volumetric_fog";
        }

        void declare_io(pass_io_builder& io) const override
        {
            // The march samples the scene depth and, through the scene
            // pass's per-frame group, the three shadow maps; the composite
            // blends over the scene colour, reading it through the blend.
            io.read("scene_depth");
            io.read("shadow_map");
            io.read("point_shadow");
            io.read("spot_shadow");
            io.read("scene_color");
            io.write("scene_color");
        }

        // Recreates the half-resolution march target and the
        // full-resolution upsample target at the new drawable size (new
        // before old) and drops the bind groups that referenced them;
        // the next enabled record() rebuilds them. No-op while the pass
        // is disabled.
        void resize(uint32_t width, uint32_t height) override;

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // Allocates the march target at ceil(size / 2) and the upsample
        // target at @p width x @p height, both rgba16f without depth.
        void create_targets(uint32_t width, uint32_t height);

        // Releases the three bind groups and forgets the depth they were
        // built against, so the next enabled record() rebuilds them.
        void release_bind_groups();

        // Packs this frame's params block (the volumetric settings, the
        // noise frame and the camera's inverse projection) and writes it
        // to @ref m_params_ubo. The only place that buffer is written;
        // prepare() calls it once per drawn frame, before the stages that
        // read it are recorded.
        void upload_params(const frame_context& ctx);

        // Builds the march, upsample and composite bind groups against
        // @p scene_depth and the current targets, remembering the depth
        // handle in @ref m_bound_depth.
        void rebuild_bind_groups(gpu::texture scene_depth);

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_march_shader{};
        gpu::shader_module m_upsample_shader{};
        gpu::shader_module m_composite_shader{};
        gpu::buffer m_vertex_buffer{};

        // The params block the march and the upsample share, rewritten
        // every drawn frame by @ref upload_params.
        gpu::buffer m_params_ubo{};

        // March slot 1: {params @ volumetric_fog_params, scene depth @
        // volumetric_fog_depth}, numbered from the shader binding table
        // because slot 0 is the scene's per-frame set. The upsample and
        // composite bind nothing of the scene's and number locally:
        // {march @0, scene depth @1, params @2} and {upsampled fog @0}.
        gpu::bind_group_layout m_march_layout{};
        gpu::bind_group_layout m_upsample_layout{};
        gpu::bind_group_layout m_composite_layout{};

        gpu::pipeline m_march_pipeline{};
        gpu::pipeline m_upsample_pipeline{};
        gpu::pipeline m_composite_pipeline{};

        gpu::render_target m_march_target{};
        gpu::texture m_march_texture{};
        gpu::render_target m_upsample_target{};
        gpu::texture m_upsample_texture{};

        gpu::bind_group m_march_bind_group{};
        gpu::bind_group m_upsample_bind_group{};
        gpu::bind_group m_composite_bind_group{};

        // The scene depth texture the bind groups were built against;
        // invalid until the first drawn frame builds them.
        gpu::texture m_bound_depth{};

        // False when the backbuffer dimensions are degenerate (no
        // settings, zero-sized window); record() then no-ops.
        bool m_enabled{false};

        // Whether this frame's record() draws, decided by prepare().
        bool m_draws{false};
    };
} // namespace rendering_engine
