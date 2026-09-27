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

    // Flat subdivided plane lying in the XY plane, centred on the origin.
    // Parameterised by (width, height, width_segments, height_segments).
    // Vertex format is position + uv + normal + tangent; every
    // normal points along +Z and UVs span [0, 1] across the surface.
    struct plane : public renderable
    {
        explicit plane(material* mat,
                       float width = 1.0f,
                       float height = 1.0f,
                       unsigned int width_segments = 1,
                       unsigned int height_segments = 1);
        ~plane() override;

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
        float m_width;
        float m_height;
        unsigned int m_width_segments;
        unsigned int m_height_segments;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};
        bool m_vertex_format_reported{false};

        std::shared_ptr<mesh_asset> m_mesh;
        // The PerDraw block the pass pushes; no buffer of its own.
        per_draw_binding m_per_draw;
    };
} // namespace rendering_engine
