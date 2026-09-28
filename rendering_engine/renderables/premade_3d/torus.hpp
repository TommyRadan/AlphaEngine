// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>

#include <core/math/transform.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;
    struct mesh_asset;

    // Torus (donut) surface.
    // @c radius is the distance from the centre of the torus to the centre
    // of the tube, @c tube is the radius of the tube itself. @c radial_segments
    // controls the tessellation around the tube cross-section, @c tubular_segments
    // the tessellation around the main ring, and @c arc sweeps a partial ring
    // (full ring at 2*pi). Each cell is two triangles. Vertex format is
    // position + uv + normal + tangent with outward-facing CCW winding.
    struct torus : public renderable
    {
        torus(asset_cache& cache,
              material* mat,
              float radius = 1.0f,
              float tube = 0.4f,
              unsigned int radial_segments = 12,
              unsigned int tubular_segments = 48,
              float arc = 6.28318530718f /* 2*pi */);
        ~torus() override;

        core::transform transform;

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

        // The cached mesh's object-space box under @ref transform (its world
        // matrix, or its own matrix only for local_bounds); false until
        // @ref upload has fetched the geometry.
        bool world_bounds(core::math::aabb& out) const final;
        bool local_bounds(core::math::aabb& out) const final;

        gpu::buffer get_vertex_buffer() const;
        gpu::buffer get_index_buffer() const;
        unsigned int get_index_count() const;

    private:
        // The cache @ref upload fetches the shared geometry through; it
        // outlives the shape.
        asset_cache* m_cache{nullptr};
        material* m_material{nullptr};
        float m_radius;
        float m_tube;
        unsigned int m_radial_segments;
        unsigned int m_tubular_segments;
        float m_arc;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};
        bool m_vertex_format_reported{false};

        std::shared_ptr<mesh_asset> m_mesh;
        // The PerDraw block the pass pushes; no buffer of its own.
        per_draw_binding m_per_draw;
    };
} // namespace rendering_engine
