// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/shadow_casters.hpp>
#include <rendering_engine/passes/shadow_settings.hpp>
#include <rendering_engine/render_proxies.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    /**
     * @brief Spot-light shadow-map pass.
     *
     * Runs ahead of the @ref scene_pass and renders the scene's depth from
     * the first shadow-casting @ref spot_light's point of view into a
     * single off-screen depth-only @c depth32_float target (a perspective
     * map, unlike the directional pass's auto-fitted orthographic
     * cascades), sized by @c rendering_engine::shadow_settings::resolution.
     * The vertical field of view is twice the caster's outer cone
     * half-angle, so the map exactly covers the cone, and the far plane
     * follows the caster's @ref spot_light::range (a fixed default when
     * the range is 0, "no cutoff"). The @ref scene_pass exposes the depth
     * texture plus the light-space view-projection matrix to the lit
     * materials through its per-frame bind group, and the lit fragment
     * shaders sample it to occlude that light's contribution.
     *
     * Like @ref shadow_pass and @ref point_shadow_pass it culls the
     * frame's mesh draws (@ref frame_context::scene_draws) and pushes the
     * PerDraw block each carries (or, for an instanced batch, reads its
     * per-instance transform stream through the instanced pipeline twin),
     * so every mesh proxy casts with no per-proxy wiring; only the
     * depth-only pipeline (vertex stage only, rasteriser depth bias
     * against acne) and the light-space matrix differ. When no spot light
     * has @c cast_shadow set the pass still clears the map and reports
     * @ref has_shadow as false so the lit shaders fall back to unshadowed
     * lighting. A draw also needs @ref mesh_draw::casts_shadow and a
     * @ref mesh_draw::layer_mask that overlaps @ref caster_mask to reach
     * the map; both default to "every mesh casts".
     */
    struct spot_shadow_pass : pass
    {
        // @p settings supplies the map resolution and the rasteriser slope
        // bias; both are fixed for the pass's lifetime.
        spot_shadow_pass(gpu::device& device, const rendering_engine::shadow_settings& settings);
        ~spot_shadow_pass() override;

        spot_shadow_pass(const spot_shadow_pass&) = delete;
        spot_shadow_pass& operator=(const spot_shadow_pass&) = delete;

        // Finds the caster, builds and uploads its light-space matrix
        // and culls and collects the casters; every accessor below
        // reports this frame from here on. Runs ahead of the scene
        // pass's prepare.
        void prepare(const frame_context& ctx) override;

        // Clears the map and draws the casters @ref prepare collected.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "spot_shadow";
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.write("spot_shadow");
        }

        // Depth texture the shadow map is rendered into. Stable for the
        // pass's lifetime so the @ref scene_pass can bake it into its
        // per-frame bind group once at construction.
        gpu::texture shadow_map() const;

        // Light-space view-projection matrix for the active caster,
        // refreshed every @ref prepare. Only meaningful when
        // @ref has_shadow is true.
        const core::math::mat4& light_view_projection() const;

        // Whether a shadow-casting spot light was found this frame and
        // the shadow map holds usable depth.
        bool has_shadow() const;

        // Index of the caster within the packed spot-light array (matching
        // @ref pack_lights ordering) so the lit shader only shadows that
        // one light. -1 when @ref has_shadow is false.
        int shadow_spot_index() const;

        // Base depth-comparison bias the lit shader slope-scales to
        // suppress shadow acne.
        float depth_bias() const;

        // Casters skipped by the last @ref prepare because their world
        // bounds fell outside the light's perspective frustum. Zero on
        // no-caster frames.
        uint32_t culled_count() const;

        // Layer bits this pass accepts casters from, on top of the
        // existing @ref mesh_draw::casts_shadow filter: a mesh draw
        // whose layer_mask shares no bit with this mask casts no shadow
        // through it. Defaults to @ref layer_all, so nothing changes until
        // a caller narrows it.
        void set_caster_mask(uint32_t mask) noexcept;
        uint32_t caster_mask() const noexcept;

    private:
        // The device this pass creates its resources on and releases them
        // through; handed in by the renderer and outlives the pass.
        gpu::device* m_device{nullptr};

        // Off-screen depth-only shadow-map target (the sampled
        // @c depth32_float attachment is its only attachment) and the
        // depth-only pipeline that fills it.
        gpu::render_target m_target{};
        gpu::texture m_depth_texture{};
        gpu::shader_module m_vertex_shader{};
        gpu::pipeline m_pipeline{};

        // The instanced twin of @ref m_pipeline for instanced casters;
        // shares the fragment stage, the light layout and the
        // fixed-function state.
        instanced_shadow_pipeline m_instanced{};

        // Per-light bind group (slot 0): the light-space view-projection
        // matrix at binding 0. The per-draw model matrix is each caster's
        // pushed PerDraw block.
        gpu::bind_group_layout m_light_layout{};
        gpu::buffer m_light_ubo{};
        gpu::bind_group m_light_bind_group{};

        // Reused across frames so the allocation persists. Collected by
        // prepare(), drawn by record().
        std::vector<draw_item> m_items;

        core::math::mat4 m_light_view_projection{};
        bool m_has_shadow{false};
        int m_shadow_spot_index{-1};
        uint32_t m_culled{0};
        uint32_t m_caster_mask{layer_all};
    };
} // namespace rendering_engine
