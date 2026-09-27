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

    // Cylinder (or truncated cone) primitive. Centered at the origin with its axis along +Y;
    // the height spans [-height/2, +height/2]. The torso (side wall) is
    // tessellated into radial_segments columns by height_segments rows
    // of quads with smooth radial normals derived from the slope between
    // the two radii. Unless open_ended, a top and a bottom cap fan are
    // appended with flat normals. A cap whose radius is 0 is skipped, so
    // a top radius of 0 yields a cone. Vertex format is position + uv +
    // normal + tangent with CCW outward winding.
    struct cylinder : public renderable
    {
        explicit cylinder(material* mat,
                          float radius_top = 1.0f,
                          float radius_bottom = 1.0f,
                          float height = 1.0f,
                          unsigned int radial_segments = 32,
                          unsigned int height_segments = 1,
                          bool open_ended = false);
        ~cylinder() override;

        core::transform transform;

        void upload() override;
        void collect_draw_items(std::vector<draw_item>& out) override;

        // The cached mesh's object-space box under @ref transform (its world
        // matrix, or its own matrix only for local_bounds); false until
        // @ref upload has fetched the geometry.
        bool world_bounds(core::math::aabb& out) const override;
        bool local_bounds(core::math::aabb& out) const override;

        gpu::buffer get_vertex_buffer() const;
        gpu::buffer get_index_buffer() const;
        unsigned int get_index_count() const;

    private:
        material* m_material{nullptr};
        float m_radius_top;
        float m_radius_bottom;
        float m_height;
        unsigned int m_radial_segments;
        unsigned int m_height_segments;
        bool m_open_ended;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};
        bool m_vertex_format_reported{false};

        std::shared_ptr<mesh_asset> m_mesh;
        // The PerDraw block and this frame's slot of it in the per-draw
        // ring; no buffer of its own.
        per_draw_binding m_per_draw;
    };
} // namespace rendering_engine
