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

#include <array>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/render_graph/frame_graph.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    struct renderable;

    // Six faces of the omni shadow, in the order the lit shader selects by the
    // major axis of (fragment - light): +X, -X, +Y, -Y, +Z, -Z.
    constexpr int point_shadow_face_count = 6;

    /**
     * @brief Omni (point-light) shadow-map pass.
     *
     * Runs ahead of the @ref scene_pass and renders the scene's depth from the
     * first shadow-casting @ref point_light into six perspective depth maps —
     * one per ±X/±Y/±Z direction (a cube map emulated with six 2D targets, so
     * no cube render-target support is required from the device). The
     * @ref scene_pass exposes the six depth textures plus the six light-space
     * view-projections and the light position to the lit materials through its
     * per-frame bind group; the lit fragment shader picks the face by the major
     * axis of the fragment-to-light vector and samples it to occlude the
     * light's contribution.
     *
     * Like @ref shadow_pass it reuses the scene-renderable registry and each
     * renderable's existing per-draw model-matrix bind group, so every scene
     * renderable casts with no per-renderable wiring. When no point light has
     * @c cast_shadow set the pass still clears the maps and reports
     * @ref has_shadow false so the lit shader falls back to unshadowed lighting.
     */
    struct point_shadow_pass : pass
    {
        explicit point_shadow_pass(std::vector<renderable*>* registry);
        ~point_shadow_pass() override;

        point_shadow_pass(const point_shadow_pass&) = delete;
        point_shadow_pass& operator=(const point_shadow_pass&) = delete;

        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return "point_shadow";
        }

        void declare_io(render_graph::pass_io_builder& io) const override
        {
            io.write("point_shadow");
        }

        // Depth texture for face @p face (0..5). Stable for the pass's lifetime
        // so the scene pass can bake the handles into its per-frame bind group.
        gpu::texture shadow_map(int face) const;

        // Light-space view-projection for face @p face, refreshed every record.
        const core::math::mat4& light_view_projection(int face) const;

        // World-space position of the active caster, refreshed every record.
        const core::math::vec3& light_position() const;

        // Whether a shadow-casting point light was found this frame.
        bool has_shadow() const;

        // Index of the caster within the packed point-light array (matching
        // pack_lights ordering) so the lit shader only shadows that light. -1
        // when has_shadow is false.
        int shadow_point_index() const;

        // Base depth-comparison bias the lit shader slope-scales.
        float depth_bias() const;

    private:
        // Non-owning back-pointer to the engine context's scene-renderable
        // registry — the same one the scene and directional shadow passes walk.
        std::vector<renderable*>* m_registry;

        std::array<gpu::render_target, point_shadow_face_count> m_targets{};
        std::array<gpu::texture, point_shadow_face_count> m_depth_textures{};

        gpu::shader_module m_vertex_shader{};
        gpu::shader_module m_fragment_shader{};
        gpu::pipeline m_pipeline{};

        gpu::bind_group_layout m_light_layout{};
        gpu::bind_group_layout m_draw_layout{};
        std::array<gpu::buffer, point_shadow_face_count> m_light_ubos{};
        std::array<gpu::bind_group, point_shadow_face_count> m_light_bind_groups{};

        std::vector<draw_item> m_items;

        std::array<core::math::mat4, point_shadow_face_count> m_light_view_projections{};
        core::math::vec3 m_light_position{0.0f, 0.0f, 0.0f};
        bool m_has_shadow{false};
        int m_shadow_point_index{-1};
    };
} // namespace rendering_engine
