// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <vector>

#include <core/math/math.hpp>
#include <core/math/transform.hpp>
#include <rendering_engine/assets/vertex.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/per_draw_ring.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct material;

    // A point cloud. It owns a list
    // of point positions (with optional per-point colours) and submits
    // a single non-indexed @ref draw_item against a point-list material
    // (@ref points_material), so the scene pass rasterizes one GL point
    // sprite per vertex. Size, tint and the optional sprite live on the
    // material; the geometry and the model transform live here.
    struct points : public renderable
    {
        // The material is non-owning; it is typically a
        // @ref points_material (its pipeline must bake point topology)
        // created by @ref rendering_engine::renderer and shared by every
        // point cloud that draws under it.
        explicit points(material* mat);
        ~points() override;

        core::transform transform;

        // Set the cloud positions; every point defaults to white and is
        // tinted by the material colour. Replaces any previous data.
        // Call @ref upload afterwards to push it to the GPU.
        void set_positions(const std::vector<core::math::vec3>& positions);

        // Set positions with a matching per-point colour list. The two
        // vectors must be the same length; a size mismatch logs and the
        // call is ignored. Call @ref upload afterwards.
        void set_positions(const std::vector<core::math::vec3>& positions, const std::vector<core::math::vec3>& colors);

        // Upload the staged point data into a GPU vertex buffer. Safe to
        // call again after a @ref set_positions to re-upload; the
        // previous buffer is released first.
        void upload() final;

        void collect_draw_items(std::vector<draw_item>& out) final;

        // Box over the uploaded points under @ref transform; false until
        // @ref upload has pushed at least one point.
        bool world_bounds(core::math::aabb& out) const final;

    private:
        material* m_material{nullptr};
        std::vector<vertex_position_color> m_vertices;

        // Object-space box over the points at the last @ref upload.
        core::math::aabb m_local_bounds{};
        bool m_has_local_bounds{false};

        gpu::buffer m_vertex_buffer{};
        // The PerDraw block and this frame's slot of it in the per-draw
        // ring; no buffer of its own.
        per_draw_binding m_per_draw;

        size_t m_vertex_count{0};
        uint32_t m_vertex_stride{0};
    };
} // namespace rendering_engine
