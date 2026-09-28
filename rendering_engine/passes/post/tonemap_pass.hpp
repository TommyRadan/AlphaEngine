// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <array>
#include <cstddef>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/post_settings.hpp>

namespace rendering_engine
{
    /**
     * @brief Maps the HDR scene-colour target into LDR for the
     *        swapchain via a selectable tonemap curve and gamma encode.
     *
     * Reads the rgba16f scene target produced by @ref scene_pass (or
     * @ref motion_blur_pass's blurred copy of it, whichever
     * @ref frame_resources::scene_color holds when this pass prepares),
     * applies @c exposure as a pre-curve scale, runs the selected
     * operator (ACES filmic by default, Reinhard, or a clamp-only linear
     * path), and gamma-2.2 encodes the result before writing to the
     * off-screen LDR target. Without this pass HDR luminance > 1.0
     * saturates to white as soon as it hits the 8-bit backbuffer.
     *
     * It resolves into @ref frame_resources::ldr_color rather
     * than straight to the swapchain so the trailing @ref fxaa_pass
     * can sample the tonemapped result as a shader input (the
     * swapchain is not sampleable). Slots into the post chain between
     * @ref scene_pass and @ref fxaa_pass. Pipeline state mirrors any
     * other fullscreen-
     * triangle post pass (depth off, blend off, no culling, no
     * vertex buffers); the shared
     * @ref fullscreen_triangle_vertex_shader emits the geometry
     * from @c gl_VertexID.
     *
     * @c exposure and the operator default to 1.0 / @ref
     * tonemap_operator::aces and are baked into the @c Tonemap UBO at
     * construction. Both are live-tunable through
     * @ref post_settings::exposure and @ref post_settings::tonemap_op:
     * @ref prepare rewrites the UBO when either differs from what it last
     * uploaded.
     *
     * Two optional stages ride on the same draw, each selected per frame
     * by picking one of four pipeline variants (the @c USE_AUTO_EXPOSURE /
     * @c USE_COLOR_GRADING keywords of @c shaders/passes/tonemap.frag.glsl)
     * rather than by a runtime branch, so an effect that is off costs
     * nothing:
     *
     *  - Eye adaptation: while @ref frame_resources::exposure is
     *    published, the exposure comes from @ref auto_exposure_pass's 1x1
     *    result instead of @ref post_settings::exposure, which applies
     *    again as soon as auto exposure is off.
     *  - Colour grading: while @ref frame_resources::grading_lut is
     *    published and @ref color_grading_settings::intensity is positive,
     *    the display-referred colour (after the curve and the gamma encode)
     *    is looked up in that strip LUT and blended with the ungraded
     *    colour by the intensity, which @ref prepare rewrites into the UBO
     *    when it changes.
     */
    struct tonemap_pass : pass
    {
        // The HDR image it maps is not a constructor input: it is looked
        // up every frame (@ref frame_resources::scene_color: the scene
        // colour, or motion blur's output while that runs), and the input
        // bind group is (re)built whenever that handle, the grading LUT or
        // the exposure texture differs from the one it was last built
        // against, so a resize that recreates a target or a toggled effect
        // is picked up without any re-plumbing.
        explicit tonemap_pass(gpu::device& device);
        ~tonemap_pass() override;

        tonemap_pass(const tonemap_pass&) = delete;
        tonemap_pass& operator=(const tonemap_pass&) = delete;

        // Picks the variant the frame draws with, rewrites the exposure,
        // the operator and the grading blend when one changed and rebinds
        // the inputs whose handles changed.
        void prepare(const frame_context& ctx) override;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::tonemap;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.read(frame_resources::scene_color);
            io.read_optional(frame_resources::exposure);
            io.read_optional(frame_resources::grading_lut);
            io.write(frame_resources::ldr_color);
        }

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // Pipeline variants, indexed by these bits: the fragment stage is
        // compiled with USE_COLOR_GRADING and / or USE_AUTO_EXPOSURE.
        static constexpr size_t variant_grading = 1;
        static constexpr size_t variant_auto_exposure = 2;
        static constexpr size_t variant_count = 4;

        // Repacks the { exposure, operator, grading intensity } block and
        // writes it to the Tonemap UBO; called by prepare() whenever a
        // value changes.
        void upload_uniforms();

        // The pipeline variant this frame draws with, picked by prepare().
        size_t m_variant{0};

        // Rebuild the input bind group against @p input_color, the Tonemap
        // UBO, @p grading_lut and @p exposure (either may be invalid when
        // the variant drawn does not sample it), remembering the three
        // handles.
        void rebuild_bind_group(gpu::texture input_color, gpu::texture grading_lut, gpu::texture exposure);

        // CPU-side mirror of the std140 @c Tonemap UBO: the float
        // exposure scale, the int operator selector and the float grading
        // blend.
        float m_exposure{1.0f};
        tonemap_operator m_operator{tonemap_operator::aces};
        float m_grading_intensity{1.0f};

        gpu::shader_module m_vertex_shader{};
        std::array<gpu::shader_module, variant_count> m_fragment_shaders{};
        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_tonemap_ubo{};
        // {HDR colour @0, Tonemap UBO @1, grading LUT @2, exposure @3},
        // shared by every variant.
        gpu::bind_group_layout m_input_layout{};
        gpu::bind_group m_input_bind_group{};
        std::array<gpu::pipeline, variant_count> m_pipelines{};

        // The textures @ref m_input_bind_group was built against; invalid
        // until the first prepare() builds the group.
        gpu::texture m_bound_input{};
        gpu::texture m_bound_grading_lut{};
        gpu::texture m_bound_exposure{};

        // The LDR target this frame resolves into, looked up by prepare().
        gpu::render_target m_target{};
    };
} // namespace rendering_engine
