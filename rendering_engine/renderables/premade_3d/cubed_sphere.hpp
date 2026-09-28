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

    // Cubed sphere ("quad sphere"): six face grids of NxN quads, each
    // vertex pushed out to the unit sphere. Eliminates the pole pinch
    // of a UV sphere — there is no preferred axis, only six face
    // patches that meet at the cube's corners with bounded distortion
    // (~1.4x at corners). Pairs naturally with cube-map textures, which
    // are sampled by the surface normal and are also free of the polar
    // singularity.
    struct cubed_sphere : public renderable
    {
        // @p subdivisions is the per-face grid resolution (NxN quads).
        // Total mesh has 6*subdivisions*subdivisions quads = 12*N*N
        // triangles.
        cubed_sphere(asset_cache& cache, material* mat, unsigned int subdivisions = 32);
        ~cubed_sphere() override;

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
        unsigned int m_subdivisions;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};
        bool m_vertex_format_reported{false};

        std::shared_ptr<mesh_asset> m_mesh;
        // The PerDraw block the pass pushes; no buffer of its own.
        per_draw_binding m_per_draw;
    };
} // namespace rendering_engine
