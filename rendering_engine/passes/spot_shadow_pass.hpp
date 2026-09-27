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

#include <vector>

#include <core/math/math.hpp>
#include <core/settings.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/shadow_casters.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>
#include <rendering_engine/renderables/draw_item.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct renderable;

    /**
     * @brief Spot-light shadow-map pass.
     *
     * Runs ahead of the @ref scene_pass and renders the scene's depth from
     * the first shadow-casting @ref spot_light's point of view into a
     * single off-screen depth-only @c depth32_float target (a perspective
     * map, unlike the directional pass's auto-fitted orthographic
     * cascades), sized by @c core::shadow_settings::resolution.
     * The vertical field of view is twice the caster's outer cone
     * half-angle, so the map exactly covers the cone, and the far plane
     * follows the caster's @ref spot_light::range (a fixed default when
     * the range is 0, "no cutoff"). The @ref scene_pass exposes the depth
     * texture plus the light-space view-projection matrix to the lit
     * materials through its per-frame bind group, and the lit fragment
     * shaders sample it to occlude that light's contribution.
     *
     * Like @ref shadow_pass and @ref point_shadow_pass it reuses the
     * scene-renderable registry and each renderable's existing per-draw
     * model-matrix bind group (or, for an instanced batch, its
     * per-instance transform stream through the instanced pipeline twin),
     * so every scene renderable casts with no per-renderable wiring; only
     * the depth-only pipeline (vertex stage only, rasteriser depth bias
     * against acne) and the light-space matrix differ. When no spot light
     * has @c cast_shadow set the pass still clears the map and reports
     * @ref has_shadow as false so the lit shaders fall back to unshadowed
     * lighting. A renderable also needs @ref renderable::casts_shadow and
     * a @ref renderable::layer_mask that overlaps @ref caster_mask to
     * reach the map; both default to "every renderable casts".
     */
    struct spot_shadow_pass : pass
    {
        // @p settings supplies the map resolution and the rasteriser slope
        // bias; both are fixed for the pass's lifetime.
        spot_shadow_pass(std::vector<renderable*>* registry, const core::shadow_settings& settings);
        ~spot_shadow_pass() override;

        spot_shadow_pass(const spot_shadow_pass&) = delete;
        spot_shadow_pass& operator=(const spot_shadow_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "spot_shadow";
        }

        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.write("spot_shadow");
        }

        // Depth texture the shadow map is rendered into. Stable for the
        // pass's lifetime so the @ref scene_pass can bake it into its
        // per-frame bind group once at construction.
        gpu::texture shadow_map() const;

        // Light-space view-projection matrix for the active caster,
        // refreshed every @ref record. Only meaningful when
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

        // Casters skipped by the last @ref record because their world
        // bounds fell outside the light's perspective frustum. Zero on
        // no-caster frames.
        uint32_t culled_count() const;

        // Layer bits this pass accepts casters from, on top of the
        // existing @ref renderable::casts_shadow filter: a renderable
        // whose layer_mask shares no bit with this mask casts no shadow
        // through it. Defaults to @ref layer_all, so nothing changes until
        // a caller narrows it.
        void set_caster_mask(uint32_t mask) noexcept;
        uint32_t caster_mask() const noexcept;

    private:
        // Non-owning back-pointer to the engine context's
        // scene-renderable registry — the same one the scene and other
        // shadow passes walk. The context outlives every pass.
        std::vector<renderable*>* m_registry;

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
        // matrix at binding 0. The per-draw model matrix (binding 1)
        // comes from each renderable's own bind group, bound at slot 1.
        gpu::bind_group_layout m_light_layout{};
        gpu::bind_group_layout m_draw_layout{};
        gpu::buffer m_light_ubo{};
        gpu::bind_group m_light_bind_group{};

        // Reused across frames so the allocation persists.
        std::vector<draw_item> m_items;

        core::math::mat4 m_light_view_projection{};
        bool m_has_shadow{false};
        int m_shadow_spot_index{-1};
        uint32_t m_culled{0};
        uint32_t m_caster_mask{layer_all};
    };
} // namespace rendering_engine
