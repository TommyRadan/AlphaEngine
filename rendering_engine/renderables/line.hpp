// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <assets/vertex.hpp>
#include <core/math/math.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct device;
    }

    struct material;

    // How successive vertices are joined into segments. @c strip joins
    // every vertex to the next (a connected
    // polyline); @c segments treats vertices as independent pairs,
    // so vertices 0-1 form one segment,
    // 2-3 the next, and so on.
    enum class line_mode
    {
        strip,
        segments,
    };

    // A line (connected strip or independent segments). It
    // owns a list of vertex positions (with optional per-vertex colours)
    // and submits a single @ref draw_item against a line-topology
    // material (@ref line_material), so the scene pass rasterizes the
    // vertices as connected or independent line segments. The line width
    // is fixed at one pixel; tint lives on the material, geometry and the
    // model transform live here.
    //
    // The backend bakes line-list topology into the pipeline, so a
    // @c strip line is expanded into an index buffer of segment pairs at
    // @ref upload time; a @c segments line draws its vertices directly,
    // two per segment.
    struct line : public renderable
    {
        // The material is non-owning; it is typically a
        // @ref line_material (its pipeline must bake line topology)
        // created by @ref rendering_engine::renderer and shared by every
        // line that draws under it.
        line(gpu::device& device, material* mat);
        ~line() override;

        core::transform transform;

        // Select how vertices are joined. Takes effect on the next
        // @ref upload; defaults to @c line_mode::strip.
        void set_mode(line_mode mode);

        // Set the line vertices; every vertex defaults to white and is
        // tinted by the material colour. Replaces any previous data.
        // Call @ref upload afterwards to push it to the GPU.
        void set_positions(const std::vector<core::math::vec3>& positions);

        // Set positions with a matching per-vertex colour list. The two
        // vectors must be the same length; a size mismatch logs and the
        // call is ignored. Call @ref upload afterwards.
        void set_positions(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // Upload the staged line data into a GPU vertex buffer (and, in
        // @c strip mode, the matching segment index buffer). Safe to
        // call again after a @ref set_positions to re-upload; the
        // previous buffers are released first.
        void upload() final;

        void collect_draw_items(std::vector<draw_item>& out) final;

        // Box over the uploaded vertices under @ref transform; false until
        // @ref upload has pushed at least one vertex.
        bool world_bounds(core::math::aabb& out) const final;

    private:
        // The device this renderable's GPU resources are created on and
        // released through; it outlives the renderable.
        gpu::device* m_device{nullptr};
        material* m_material{nullptr};
        line_mode m_mode{line_mode::strip};
        std::vector<assets::vertex_position_color> m_vertices;

        // Object-space box over the vertices at the last @ref upload.
        core::math::aabb m_local_bounds{};
        bool m_has_local_bounds{false};

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_index_buffer{};
        // The PerDraw block the pass pushes; no buffer of its own.
        per_draw_binding m_per_draw;

        size_t m_vertex_count{0};
        size_t m_index_count{0};
        uint32_t m_vertex_stride{0};
    };
} // namespace rendering_engine
