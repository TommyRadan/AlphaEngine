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
     * looked up in the published @ref frame_resources::scene_view the way
     * the debug pass takes it, plus its own parameters and the scene depth
     * at slot 1. The medium is the
     * scene's height fog (@ref fog_settings::height_density, falloff and
     * reference height) times @ref volumetric_fog_settings::density_scale;
     * while it runs the lit materials start their analytic height fog at
     * the march's max distance (see @ref volumetric_fog_active), so the
     * two never attenuate the same stretch twice.
     *
     * Always in the pass list, but @ref record draws nothing unless
     * @ref frame_context::post enables it, a camera is active and the
     * height fog has a density, so with the default settings the frame
     * is unchanged. Each view gets its targets (sized to the view), its
     * params block and its bind groups (see @ref view_data), built the
     * first frame the fog marches for it.
     */
    struct volumetric_fog_pass : pass
    {
        // @p frame_layout is the scene pass's per-frame layout, which the
        // march pipeline reserves slot 0 for. The group bound there is not a
        // constructor input: the march binds the scene pass's jittered
        // per-frame group (the view the scene depth was rasterised with),
        // looked up in @ref frame_resources::scene_view each frame after
        // the scene pass refilled its buffers, so it always reflects the
        // current view, lights and shadow casters. Nor is the scene depth:
        // it is looked up every frame (@ref frame_resources::scene_depth),
        // and the bind groups sampling it are rebuilt whenever that handle
        // changes.
        volumetric_fog_pass(gpu::device& device, gpu::bind_group_layout frame_layout);
        ~volumetric_fog_pass() override;

        volumetric_fog_pass(const volumetric_fog_pass&) = delete;
        volumetric_fog_pass& operator=(const volumetric_fog_pass&) = delete;

        // Decides whether the frame marches, writes the params block and
        // rebinds the scene depth when its handle changed.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::volumetric_fog;
        }

        void declare_io(pass_io_builder& io) const override
        {
            // The march samples the scene depth and, through the scene
            // pass's per-frame group, the three shadow maps; the composite
            // blends over the scene colour, reading it through the blend.
            io.read(frame_resources::scene_depth);
            io.read_optional(frame_resources::scene_view);
            io.read_optional(frame_resources::directional_shadow);
            io.read_optional(frame_resources::point_shadow);
            io.read_optional(frame_resources::spot_shadow);
            io.read(frame_resources::scene_color);
            io.write(frame_resources::scene_color);
        }

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // What the pass keeps per view: the params block the march and
        // the upsample share (rewritten every drawn frame by
        // @ref upload_params), the half-resolution march target and the
        // full-resolution upsample target at the view's size (@ref width x
        // @ref height), the three bind groups over them and the scene
        // depth they were built against (invalid until the view's first
        // drawn frame builds them).
        struct view_data final : pass_view_state
        {
            explicit view_data(gpu::device& device);
            ~view_data() override;

            view_data(const view_data&) = delete;
            view_data& operator=(const view_data&) = delete;

            // (Re)allocates the march target at ceil(size / 2) and the
            // upsample target at @p width x @p height, both rgba16f without
            // depth, new before old, and drops the bind groups that
            // referenced the old ones.
            void resize(uint32_t width, uint32_t height);

            // Releases the three bind groups and forgets the depth they
            // were built against, so the next drawn frame rebuilds them.
            void release_bind_groups();

            gpu::device* device{nullptr};
            gpu::buffer params_ubo{};
            gpu::render_target march_target{};
            gpu::texture march_texture{};
            gpu::render_target upsample_target{};
            gpu::texture upsample_texture{};
            gpu::bind_group march_bind_group{};
            gpu::bind_group upsample_bind_group{};
            gpu::bind_group composite_bind_group{};
            gpu::texture bound_depth{};
            uint32_t width{0};
            uint32_t height{0};
        };

        // Packs this frame's params block (the volumetric settings, the
        // noise frame and the camera's inverse projection) and writes it
        // to @p view's params UBO. The only place that buffer is written;
        // prepare() calls it once per drawn frame, before the stages that
        // read it are recorded.
        static void upload_params(const frame_context& ctx, const view_data& view);

        // Builds @p view's march, upsample and composite bind groups
        // against @p scene_depth and its current targets, remembering the
        // depth handle.
        void rebuild_bind_groups(view_data& view, gpu::texture scene_depth);

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_march_shader{};
        gpu::shader_module m_upsample_shader{};
        gpu::shader_module m_composite_shader{};
        gpu::buffer m_vertex_buffer{};

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

        // The scene pass's jittered per-frame group the march binds at
        // slot 0 and the scene colour target the composite blends into,
        // looked up by prepare() on a drawn frame.
        gpu::bind_group m_frame_group{};
        gpu::render_target m_target{};

        // Whether this frame's record() draws, decided by prepare().
        bool m_draws{false};
    };
} // namespace rendering_engine
