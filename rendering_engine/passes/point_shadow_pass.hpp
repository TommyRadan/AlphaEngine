// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <core/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/passes/frame_resources.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/shadow_casters.hpp>
#include <rendering_engine/passes/shadow_settings.hpp>
#include <rendering_engine/render_proxies.hpp>
#include <rendering_engine/renderables/draw_item.hpp>

namespace rendering_engine
{
    /**
     * @brief Omni (point-light) shadow-map pass.
     *
     * Runs ahead of the @ref scene_pass and renders the scene's depth from the
     * first shadow-casting @ref point_light into one depth cube map: six
     * depth-only face targets over the one cube texture, each a 90-degree
     * perspective along ±X/±Y/±Z with the up vector the cube-map face
     * convention demands, so a @c samplerCube lookup along
     * (fragment - light) lands on the face and texel that saw the
     * fragment. It publishes the cube with the six face view-projections,
     * the light position and the faces' near / far planes as
     * @ref frame_resources::point_shadow, which the @ref scene_pass hands
     * the lit materials through its per-frame bind group; the lit fragment
     * shader reconstructs the receiver's face depth from the planes and
     * compares it against the sampled depth.
     *
     * Like @ref shadow_pass it culls the frame's mesh draws
     * (@ref frame_context::scene_draws) and pushes the PerDraw block each
     * carries (or, for an instanced batch, reads its per-instance transform
     * stream through the instanced pipeline twin), so every mesh proxy
     * casts with no per-proxy wiring. The faces' far plane follows the
     * caster's @ref point_light::range (a fixed default when the range is
     * 0, "no cutoff"), so the depth precision is spent on the volume the
     * light can actually reach. When no point light has @c cast_shadow set the pass
     * still clears the faces and publishes the cube as inactive
     * (@ref point_shadow_data::active) so the lit shader falls back to
     * unshadowed lighting. A draw also needs
     * @ref mesh_draw::casts_shadow and a @ref mesh_draw::layer_mask that
     * overlaps @ref caster_mask to reach the map; both default to "every
     * mesh casts".
     */
    struct point_shadow_pass : pass
    {
        // @p settings supplies the face resolution (half the configured
        // shadow resolution) and the rasteriser slope bias; both are fixed
        // for the pass's lifetime.
        point_shadow_pass(gpu::device& device, const rendering_engine::shadow_settings& settings);
        ~point_shadow_pass() override;

        point_shadow_pass(const point_shadow_pass&) = delete;
        point_shadow_pass& operator=(const point_shadow_pass&) = delete;

        // Finds the caster, collects the casters with their bounds,
        // refreshes and uploads the six face matrices, culls each caster
        // against each face and publishes the cube and the faces
        // (@ref frame_resources::point_shadow) for the scene pass, which
        // prepares after it.
        void prepare(const frame_context& ctx) override;

        // Clears every face and draws into it the casters @ref prepare
        // found reaching it.
        void record(gpu::command_encoder& encoder, const frame_context& ctx) override;

        const char* name() const override
        {
            return builtin_passes::point_shadow;
        }

        void declare_io(pass_io_builder& io) const override
        {
            io.write(frame_resources::point_shadow);
        }

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

        // One shadow caster's slice of @ref m_items plus the world bounds it
        // reported, recorded once per frame so each face culls against its
        // own frustum without re-walking the draws, and the faces that
        // culling found it reaching (bit n for face n), which record()
        // draws it into. @c bounded is false for a draw without bounds; it
        // casts into every face.
        struct caster_range
        {
            std::size_t first{0};
            std::size_t count{0};
            bool bounded{false};
            core::math::aabb bounds{};
            uint32_t faces{0};
        };

        // Publishes this frame's cube and faces
        // (@ref frame_resources::point_shadow); the end of every prepare().
        void publish(const frame_context& ctx) const;

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
        std::array<gpu::buffer, point_shadow_face_count> m_light_ubos{};
        std::array<gpu::bind_group, point_shadow_face_count> m_light_bind_groups{};

        // Reused across frames so the allocations persist. Collected once
        // per frame by prepare(), ahead of the six faces.
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
