// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <memory>

#include <core/math/transform.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/per_draw_ring.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;

    // Flat disc lying in the XY plane. Built from a
    // centre vertex and a triangle fan of rim vertices. Vertex format is
    // position + uv + normal + tangent; every normal points along +Z. Front faces are
    // CCW when viewed from +Z.
    struct circle : public renderable
    {
        explicit circle(material* mat,
                        float radius = 1.0f,
                        unsigned int segments = 32,
                        float theta_start = 0.0f,
                        float theta_length = 6.28318530718f);
        ~circle() override;

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
        unsigned int m_segments;
        float m_theta_start;
        float m_theta_length;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};
        bool m_vertex_format_reported{false};

        std::shared_ptr<mesh_asset> m_mesh;
        // The PerDraw block and this frame's slot of it in the per-draw
        // ring; no buffer of its own.
        per_draw_binding m_per_draw;
    };
} // namespace rendering_engine
