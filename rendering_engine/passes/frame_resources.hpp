// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file frame_resources.hpp
 * @brief The resources the renderer and the built-in passes publish in the
 *        frame's @ref resource_store, and the values they hold.
 *
 * Every key below is published once per frame by the renderer (before any
 * pass prepares) or by the pass that produces it (from its prepare), and
 * is absent on a frame nothing published it: a pass that is not in the
 * list, is disabled, or has nothing to hand on this frame. Its consumers
 * then fall back as their declarations say (@ref pass_io_builder).
 */

#pragma once

#include <array>
#include <cstdint>

#include <core/math/mat4.hpp>
#include <core/math/vec3.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/resource_store.hpp>
#include <rendering_engine/passes/shadow_settings.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct command_encoder;
        struct render_pass_descriptor;
    } // namespace gpu

    // Upper bound of the directional cascades: the lit shaders' Shadow
    // block holds this many light-space matrices and split depths
    // (SHADOW_MAX_CASCADES in shaders/include/shadows.glsl).
    constexpr int max_shadow_cascades = static_cast<int>(shadow_settings::max_cascade_count);

    // Six faces of the omni shadow cube, in cube-map face order: +X, -X,
    // +Y, -Y, +Z, -Z (the order of @c gpu::cube_face).
    constexpr int point_shadow_face_count = 6;

    /// A colour render target and its colour attachment, sampleable once drawn.
    struct color_target
    {
        gpu::render_target target{};
        gpu::texture texture{};
    };

    /**
     * @brief Records the depth-only share of a scene draw list into a pass
     *        the caller describes.
     *
     * The scene pass implements it, so the depth pre-pass lays down the
     * depth of exactly the items, per-draw blocks and order the scene pass
     * shades (see @ref scene_view_data::depth_prepass).
     */
    struct depth_prepass_source
    {
        /**
         * @brief Begins @p descriptor's pass on @p encoder, draws the
         *        list's pre-passed items through their depth-only
         *        pipelines and ends the pass.
         */
        virtual void record_depth_prepass(gpu::command_encoder& encoder,
                                          const gpu::render_pass_descriptor& descriptor) const = 0;

    protected:
        ~depth_prepass_source() = default;
    };

    /**
     * @brief The view the scene pass rendered this frame, for the passes
     *        that draw with the same camera, lights and shadows.
     */
    struct scene_view_data
    {
        // The per-frame layout the lit materials reserve slot 0 for.
        gpu::bind_group_layout frame_layout{};

        // The per-frame group at slot 0: the view_globals block with the
        // temporal-AA jitter the scene was rasterised with, the lights and
        // the shadow blocks and maps.
        gpu::bind_group frame_group{};

        // The same group with the view unjittered, for passes that draw
        // after the TAA resolve (the debug pass); the same handle as
        // @ref frame_group while temporal AA is off.
        gpu::bind_group overlay_frame_group{};

        // The scene pass's draw list, as the depth pre-pass records it.
        // Filled once the scene pass prepared, so the pre-pass, which
        // prepares ahead of it, looks it up while it records.
        const depth_prepass_source* depth_prepass{nullptr};
    };

    /**
     * @brief This frame's directional shadow: the cascade depth array and
     *        the fit the lit shaders sample it with.
     */
    struct directional_shadow_data
    {
        // The depth 2D array, one layer per cascade, and the comparison
        // sampler the lit shaders read it through (sampler2DArrayShadow);
        // both stable for the producing pass's lifetime.
        gpu::texture map{};
        gpu::sampler sampler{};

        // Whether a shadow-casting directional light was found and the
        // map holds usable depth; everything below is meaningful only then.
        bool active{false};

        // Cascades holding depth: the configured count with a camera, one
        // for the camera-less fallback box.
        int cascade_count{0};

        // Index of the caster in the packed directional-light array, so
        // the lit shader shadows that one light only.
        int light_index{-1};

        // Hardware-filtered PCF taps per side of the lit shader's kernel.
        uint32_t pcf_kernel{1};

        // Width of the band before each split over which the lit shader
        // cross-fades into the next cascade, as a fraction of the
        // cascade's depth range.
        float cascade_blend{0.0f};

        // Per cascade: the light-space view-projection, the view depth at
        // which the cascade ends and its receiver-side depth bias.
        std::array<core::math::mat4, max_shadow_cascades> light_view_projection{};
        std::array<float, max_shadow_cascades> split_depth{};
        std::array<float, max_shadow_cascades> depth_bias{};

        // Caster / cascade pairs culled this frame, for the render stats.
        uint32_t culled{0};
    };

    /**
     * @brief This frame's omni (point-light) shadow: the depth cube and the
     *        face matrices the lit shaders compare against.
     */
    struct point_shadow_data
    {
        // The depth cube map; stable for the producing pass's lifetime.
        gpu::texture map{};

        // Whether a shadow-casting point light was found; everything below
        // is meaningful only then.
        bool active{false};

        // Index of the caster in the packed point-light array.
        int light_index{-1};

        // The six faces' light-space view-projections, in cube-map face
        // order.
        std::array<core::math::mat4, point_shadow_face_count> face_view_projection{};

        // The caster's world position, and the faces' near / far planes the
        // lit shader reconstructs a face's stored depth from.
        core::math::vec3 light_position{0.0f, 0.0f, 0.0f};
        float near_plane{0.0f};
        float far_plane{0.0f};

        // Base depth-comparison bias the lit shader slope-scales.
        float depth_bias{0.0f};

        // Caster / face pairs culled this frame, for the render stats.
        uint32_t culled{0};
    };

    /**
     * @brief This frame's spot-light shadow: the depth map and the one
     *        perspective matrix it was rendered with.
     */
    struct spot_shadow_data
    {
        // The depth map; stable for the producing pass's lifetime.
        gpu::texture map{};

        // Whether a shadow-casting spot light was found; everything below
        // is meaningful only then.
        bool active{false};

        // Index of the caster in the packed spot-light array.
        int light_index{-1};

        core::math::mat4 light_view_projection{};

        // Base depth-comparison bias the lit shader slope-scales.
        float depth_bias{0.0f};

        // Casters culled this frame, for the render stats.
        uint32_t culled{0};
    };

    /**
     * @brief The keys of the renderer's and the built-in passes' resources.
     *
     * A custom pass looks these up and declares them like the built-in
     * ones do, and may publish resources of its own under keys it declares
     * itself.
     */
    namespace frame_resources
    {
        // The swapchain image this frame presents; the renderer publishes
        // it and imports it as valid at frame start. FXAA writes the final
        // image into it, the UI and debug passes composite on top.
        inline constexpr resource_key<gpu::render_target> swapchain{"swapchain"};

        // The HDR scene colour: the renderer's off-screen rgba16f target
        // (with the depth below), which the scene pass clears and draws
        // into and the skybox, volumetric fog and bloom add to. A post pass
        // that cannot work in place republishes it with its own output
        // (motion blur does), and the passes after it read that instead.
        inline constexpr resource_key<color_target> scene_color{"scene_color"};

        // The depth attachment of the scene colour target, sampleable once
        // the scene and skybox passes are done with it; looked up from the
        // target every frame, so a resize that swaps the attachment reaches
        // every consumer.
        //
        // Encoding: non-linear depth24, .r in [0, 1], holding the NDC z
        // itself. The camera's projection comes from
        // core::math::perspective, whose clip-space depth spans [0, w]
        // (NDC z in [0, 1]), and the viewport depth range is [0, 1], so a
        // clip-space reprojection takes the sampled value unchanged.
        // shaders/include/depth_utils.glsl ships the GLSL to linearise it:
        // linearize_depth(d, near, far) =
        // near * far / (far - d * (far - near)), the positive view-space
        // distance in [near, far]. The depth pre-pass, on frames it runs,
        // and otherwise the scene pass clear it to 1.0, so untouched
        // background texels linearise to the far plane.
        //
        // Synchronisation is the backend's concern: the Vulkan render-pass
        // cache rests an off-screen depth attachment in
        // SHADER_READ_ONLY_OPTIMAL between render passes (a pass that loads
        // it, the skybox, resumes from and returns to that layout).
        inline constexpr resource_key<gpu::texture> scene_depth{"scene_depth"};

        // The off-screen rgba8 LDR target tonemap resolves into; TAA and
        // FXAA sample it, since the swapchain cannot be sampled.
        inline constexpr resource_key<color_target> ldr_color{"ldr_color"};

        // The colour-grading strip LUT (see @c color_grading_settings),
        // published by the renderer while one is loaded; tonemap applies it.
        inline constexpr resource_key<gpu::texture> grading_lut{"grading_lut"};

        // The shadow passes' maps and fits, which the scene pass binds and
        // uploads for the lit materials.
        inline constexpr resource_key<directional_shadow_data> directional_shadow{"directional_shadow"};
        inline constexpr resource_key<point_shadow_data> point_shadow{"point_shadow"};
        inline constexpr resource_key<spot_shadow_data> spot_shadow{"spot_shadow"};

        // Published by the depth pre-pass on a frame it lays the opaque
        // depth down, holding the depth-only target it clears and fills:
        // the scene pass then loads that depth instead of clearing it.
        inline constexpr resource_key<gpu::render_target> depth_prepass{"depth_prepass"};

        // The scene pass's view (see @ref scene_view_data).
        inline constexpr resource_key<scene_view_data> scene_view{"scene_view"};

        // Per-pixel motion vectors (signed UV displacement in xy) the
        // velocity pass writes while temporal AA or motion blur reads them.
        inline constexpr resource_key<gpu::texture> velocity{"velocity"};

        // The 1x1 eye-adaptation result (r: adapted EV100, g: log2 of the
        // exposure), published while auto exposure holds a valid one;
        // tonemap takes its exposure from it then.
        inline constexpr resource_key<gpu::texture> exposure{"exposure"};

        // This frame's temporal-AA resolve, which FXAA samples in place of
        // the LDR target while it is published.
        inline constexpr resource_key<gpu::texture> taa_resolve{"taa_resolve"};
    } // namespace frame_resources
} // namespace rendering_engine
