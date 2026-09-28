// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <core/math/transform.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct asset_cache;
    struct material;
    struct mesh_asset;

    // Generic polyhedron generator.
    // Takes a base mesh expressed as a flat list of vertex positions (x, y, z
    // triplets) and a flat list of triangle indices, subdivides each base
    // triangle @c detail times, projects every resulting vertex onto the
    // sphere of the given @c radius, and derives smooth (sphere) normals plus
    // spherical UVs.
    //
    // Vertex format is position + uv + normal + tangent. Triangles are emitted with CCW
    // outward winding, matching the convention documented in sphere.cpp. The
    // four platonic-solid wrappers (tetrahedron, octahedron, icosahedron,
    // dodecahedron) feed canonical base tables into this generator.
    struct polyhedron : public renderable
    {
        polyhedron(asset_cache& cache,
                   material* mat,
                   std::vector<float> base_vertices,
                   std::vector<uint32_t> base_indices,
                   float radius = 1.0f,
                   unsigned int detail = 0);
        ~polyhedron() override;

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
        std::vector<float> m_base_vertices;
        std::vector<uint32_t> m_base_indices;
        float m_radius;
        unsigned int m_detail;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};
        bool m_vertex_format_reported{false};

        // Shared geometry from the asset cache, keyed by base tables + radius +
        // detail; freed when the last polyhedron referencing it is destroyed.
        std::shared_ptr<mesh_asset> m_mesh;
        // The PerDraw block the pass pushes; no buffer of its own.
        per_draw_binding m_per_draw;
    };
} // namespace rendering_engine
