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
#include <cstddef>
#include <cstdint>
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

    // Six faces of the omni shadow cube, in cube-map face order: +X, -X,
    // +Y, -Y, +Z, -Z (the order of @c gpu::cube_face).
    constexpr int point_shadow_face_count = 6;

    /**
     * @brief Omni (point-light) shadow-map pass.
     *
     * Runs ahead of the @ref scene_pass and renders the scene's depth from the
     * first shadow-casting @ref point_light into one depth cube map: six
     * depth-only face targets over the one cube texture, each a 90-degree
     * perspective along ±X/±Y/±Z with the up vector the cube-map face
     * convention demands, so a @c samplerCube lookup along
     * (fragment - light) lands on the face and texel that saw the
     * fragment. The @ref scene_pass exposes the cube plus the six face
     * view-projections, the light position and the faces' near / far
     * planes to the lit materials through its per-frame bind group; the
     * lit fragment shader reconstructs the receiver's face depth from the
     * planes and compares it against the sampled depth.
     *
     * Like @ref shadow_pass it reuses the scene-renderable registry and each
     * renderable's existing per-draw model-matrix bind group (or, for an
     * instanced batch, its per-instance transform stream through the
     * instanced pipeline twin), so every scene renderable casts with no
     * per-renderable wiring. The faces' far plane follows the caster's
     * @ref point_light::range (a fixed default when the range is 0, "no
     * cutoff"), so the depth precision is spent on the volume the light can
     * actually reach. When no point light has @c cast_shadow set the pass
     * still clears the faces and reports @ref has_shadow false so the lit
     * shader falls back to unshadowed lighting. A renderable also needs
     * @ref renderable::casts_shadow and a @ref renderable::layer_mask that
     * overlaps @ref caster_mask to reach the map; both default to "every
     * renderable casts".
     */
    struct point_shadow_pass : pass
    {
        // @p settings supplies the face resolution (half the configured
        // shadow resolution) and the rasteriser slope bias; both are fixed
        // for the pass's lifetime.
        point_shadow_pass(std::vector<renderable*>* registry, const core::shadow_settings& settings);
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

        // The depth cube map every face renders into. Stable for the pass's
        // lifetime so the scene pass can bake the handle into its per-frame
        // bind group.
        gpu::texture shadow_map() const;

        // Light-space view-projection for face @p face, refreshed every record.
        const core::math::mat4& light_view_projection(int face) const;

        // World-space position of the active caster, refreshed every record.
        const core::math::vec3& light_position() const;

        // Near / far planes of the six face frustums, refreshed every record
        // (the far plane follows the caster's range); the lit shader
        // reconstructs a face's stored depth from them.
        float shadow_near() const;
        float shadow_far() const;

        // Whether a shadow-casting point light was found this frame.
        bool has_shadow() const;

        // Index of the caster within the packed point-light array (matching
        // pack_lights ordering) so the lit shader only shadows that light. -1
        // when has_shadow is false.
        int shadow_point_index() const;

        // Base depth-comparison bias the lit shader slope-scales.
        float depth_bias() const;

        // Caster / face pairs skipped by the last @ref record because the
        // caster's world bounds fell outside that face's frustum (a caster
        // outside every face counts six times). Zero on no-caster frames.
        uint32_t culled_count() const;

        // Layer bits this pass accepts casters from, on top of the
        // existing @ref renderable::casts_shadow filter: a renderable
        // whose layer_mask shares no bit with this mask casts no shadow
        // through it. Defaults to @ref layer_all, so nothing changes until
        // a caller narrows it.
        void set_caster_mask(uint32_t mask) noexcept;
        uint32_t caster_mask() const noexcept;

    private:
        // One shadow caster's slice of @ref m_items plus the world bounds it
        // reported, recorded once per frame so each face culls against its
        // own frustum without re-walking the registry. @c bounded is false
        // for a renderable that reports no bounds; it casts into every face.
        struct caster_range
        {
            std::size_t first{0};
            std::size_t count{0};
            bool bounded{false};
            core::math::aabb bounds{};
        };

        // Non-owning back-pointer to the renderer's scene-renderable
        // registry — the same one the scene and directional shadow passes walk.
        std::vector<renderable*>* m_registry;

        // The depth cube and the six depth-only targets attached to its
        // faces. The cube is owned here (the targets import it), so it is
        // released after them.
        gpu::texture m_depth_texture{};
        std::array<gpu::render_target, point_shadow_face_count> m_targets{};

        gpu::shader_module m_vertex_shader{};
        gpu::pipeline m_pipeline{};

        // The instanced twin of @ref m_pipeline for instanced casters.
        instanced_shadow_pipeline m_instanced{};

        gpu::bind_group_layout m_light_layout{};
        gpu::bind_group_layout m_draw_layout{};
        std::array<gpu::buffer, point_shadow_face_count> m_light_ubos{};
        std::array<gpu::bind_group, point_shadow_face_count> m_light_bind_groups{};

        // Reused across frames so the allocations persist. Collected once
        // per frame ahead of the six faces.
        std::vector<draw_item> m_items;
        std::vector<caster_range> m_casters;

        std::array<core::math::mat4, point_shadow_face_count> m_light_view_projections{};
        core::math::vec3 m_light_position{0.0f, 0.0f, 0.0f};
        // The active caster's face far plane; only meaningful while
        // m_has_shadow is set.
        float m_light_far{0.0f};
        bool m_has_shadow{false};
        int m_shadow_point_index{-1};
        uint32_t m_culled{0};
        uint32_t m_caster_mask{layer_all};
    };
} // namespace rendering_engine
