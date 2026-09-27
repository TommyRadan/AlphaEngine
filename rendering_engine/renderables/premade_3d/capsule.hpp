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
    struct material;
    struct mesh_asset;

    // Capsule aligned to +Y and centred at the origin: a straight
    // cylindrical body of height @c length (spanning [-length/2,
    // +length/2]) capped by two hemispheres of @c radius. The surface is
    // generated as a single seamless lat/lon grid — the top hemisphere,
    // the cylindrical body, and the bottom hemisphere share rings so
    // there are no cracks. Vertex format is position + uv + normal + tangent; UVs
    // wrap radially and run along the axis. Parameterised by
    // (radius, length, cap_segments, radial_segments).
    struct capsule : public renderable
    {
        explicit capsule(material* mat,
                         float radius = 0.5f,
                         float length = 1.0f,
                         unsigned int cap_segments = 8,
                         unsigned int radial_segments = 16);
        ~capsule() override;

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
        material* m_material{nullptr};
        float m_radius;
        float m_length;
        unsigned int m_cap_segments;
        unsigned int m_radial_segments;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};
        bool m_vertex_format_reported{false};

        // Shared geometry from the asset cache, keyed by radius, length and
        // segment counts; freed when the last capsule referencing it is
        // destroyed.
        std::shared_ptr<mesh_asset> m_mesh;
        // The PerDraw block the pass pushes; no buffer of its own.
        per_draw_binding m_per_draw;
    };
} // namespace rendering_engine
