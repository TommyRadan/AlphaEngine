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
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    struct renderable;

    /**
     * @brief Directional-light shadow-map pass.
     *
     * Runs ahead of the @ref scene_pass and renders the scene's depth
     * from the first shadow-casting @ref directional_light's point of
     * view into an off-screen @c depth32_float target (the shadow map).
     * The @ref scene_pass exposes that depth texture plus the
     * light-space view-projection matrix to the lit materials through
     * its per-frame bind group, and the lit fragment shaders sample it
     * to occlude that light's contribution.
     *
     * The light's view is an orthographic box centred on the world
     * origin and oriented along the light direction — the standard
     * starting point for a single directional caster. When no
     * directional light has @c cast_shadow set the pass still clears
     * the map and reports @ref has_shadow as false so the lit shaders
     * fall back to unshadowed lighting.
     *
     * The pass reuses the same scene-renderable registry as the
     * @ref scene_pass and the per-draw model-matrix bind group each
     * renderable already builds (binding 1), so every scene renderable
     * casts without any per-renderable wiring; only the depth-only
     * pipeline and the light-space matrix differ.
     */
    struct shadow_pass : pass
    {
        explicit shadow_pass(std::vector<renderable*>* registry);
        ~shadow_pass() override;

        shadow_pass(const shadow_pass&) = delete;
        shadow_pass& operator=(const shadow_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        // Depth texture the shadow map is rendered into. Stable for the
        // pass's lifetime so the @ref scene_pass can bake it into its
        // per-frame bind group once at construction.
        gpu::texture shadow_map() const;

        // Light-space view-projection matrix for the active caster,
        // refreshed every @ref record. Only meaningful when
        // @ref has_shadow is true.
        const core::math::mat4& light_view_projection() const;

        // Whether a shadow-casting directional light was found this
        // frame and the shadow map holds usable depth.
        bool has_shadow() const;

        // Index of the caster within the packed directional-light array
        // (matching @ref pack_lights ordering) so the lit shader only
        // shadows that one light. -1 when @ref has_shadow is false.
        int shadow_light_index() const;

        // Base depth-comparison bias the lit shader slope-scales to
        // suppress shadow acne.
        float depth_bias() const;

    private:
        // Non-owning back-pointer to the engine context's
        // scene-renderable registry — the same one the scene pass
        // walks. The context outlives every pass.
        std::vector<renderable*>* m_registry;

        // Off-screen shadow-map target (small colour attachment plus
        // the sampled @c depth32_float depth attachment) and the
        // depth-only pipeline that fills it.
        gpu::render_target m_target{};
        gpu::texture m_depth_texture{};
        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::pipeline m_pipeline{};

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
        int m_shadow_light_index{-1};
    };
} // namespace rendering_engine
