// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/box.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <assets/tangent.hpp>
#include <assets/vertex.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <rendering_engine/resources/cache_key.hpp>

rendering_engine::box::box(asset_cache& cache,
                           material* mat,
                           float width,
                           float height,
                           float depth,
                           unsigned int width_segments,
                           unsigned int height_segments,
                           unsigned int depth_segments)
    : mesh_source{mat, "box"}, m_width{width}, m_height{height}, m_depth{depth},
      m_width_segments{width_segments < 1 ? 1 : width_segments},
      m_height_segments{height_segments < 1 ? 1 : height_segments},
      m_depth_segments{depth_segments < 1 ? 1 : depth_segments}
{
    fetch_mesh(cache);
}

void rendering_engine::box::fetch_mesh(asset_cache& cache)
{
    // Build and upload through the asset cache, keyed by dimensions and
    // segment counts so two boxes of the same geometry share one upload. The
    // builder only runs on a cache miss.
    set_mesh(cache.get_or_create_mesh(
        "box:" + cache_key_number(m_width) + "x" + cache_key_number(m_height) + "x" + cache_key_number(m_depth) + ":" +
            cache_key_number(m_width_segments) + "x" + cache_key_number(m_height_segments) + "x" +
            cache_key_number(m_depth_segments) + ":" +
            assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
        [this]
        {
            using core::math::vec2;
            using core::math::vec3;

            std::vector<assets::vertex_position_uv_normal> vertices;
            std::vector<uint32_t> indices;

            // Builds one tessellated, axis-aligned face as a grid of
            // grid_u x grid_v segments. @c u_axis and @c v_axis are the unit
            // basis vectors spanning the face plane; @c w_dir is the outward
            // normal direction. @c u_len / @c v_len are the face extents along
            // those axes and @c w_off the (signed) distance from the origin to
            // the face plane. The face is centered on the origin via the half
            // extents. Winding is CCW when viewed
            // from outside (along -w_dir toward the face), matching the sphere
            // convention so backface culling treats all primitives alike.
            const auto build_face = [&](const vec3& u_axis,
                                        const vec3& v_axis,
                                        const vec3& w_dir,
                                        float u_len,
                                        float v_len,
                                        float w_off,
                                        unsigned int grid_u,
                                        unsigned int grid_v)
            {
                const auto base = static_cast<uint32_t>(vertices.size());
                const float half_u = u_len * 0.5f;
                const float half_v = v_len * 0.5f;

                for (unsigned int iy = 0; iy <= grid_v; ++iy)
                {
                    const float ty = static_cast<float>(iy) / static_cast<float>(grid_v);
                    const float v_pos = ty * v_len - half_v;

                    for (unsigned int ix = 0; ix <= grid_u; ++ix)
                    {
                        const float tx = static_cast<float>(ix) / static_cast<float>(grid_u);
                        const float u_pos = tx * u_len - half_u;

                        assets::vertex_position_uv_normal vertex;
                        vertex.pos = u_axis * u_pos + v_axis * v_pos + w_dir * w_off;
                        vertex.normal = w_dir;
                        vertex.uv = vec2{tx, 1.0f - ty};
                        vertices.push_back(vertex);
                    }
                }

                const unsigned int columns = grid_u + 1;
                for (unsigned int iy = 0; iy < grid_v; ++iy)
                {
                    for (unsigned int ix = 0; ix < grid_u; ++ix)
                    {
                        const uint32_t a = base + iy * columns + ix;
                        const uint32_t b = a + 1;
                        const uint32_t c = a + columns;
                        const uint32_t d = c + 1;

                        // a/b advance along +u, c/d along +v, and the outward
                        // normal is cross(u, v); the triangles must therefore
                        // wind a->d->c / a->b->d to come out CCW when viewed
                        // from outside (the a->c->d / a->d->b order is CW and
                        // would be culled, turning the box inside-out).
                        indices.push_back(a);
                        indices.push_back(d);
                        indices.push_back(c);

                        indices.push_back(a);
                        indices.push_back(b);
                        indices.push_back(d);
                    }
                }
            };

            const vec3 axis_x{1.0f, 0.0f, 0.0f};
            const vec3 axis_y{0.0f, 1.0f, 0.0f};
            const vec3 axis_z{0.0f, 0.0f, 1.0f};
            const float half_w = m_width * 0.5f;
            const float half_h = m_height * 0.5f;
            const float half_d = m_depth * 0.5f;

            // +Z front: u = +x, v = +y. +X right: u = -z, v = +y.
            // -Z back: u = -x, v = +y. -X left: u = +z, v = +y.
            // +Y top: u = +x, v = -z. -Y bottom: u = +x, v = +z.
            // The u/v axes are chosen so the cross(u, v) points along the
            // outward normal, which keeps the CCW-from-outside winding above
            // consistent across all six faces.
            build_face(axis_x, axis_y, axis_z, m_width, m_height, half_d, m_width_segments, m_height_segments);
            build_face(-axis_z, axis_y, axis_x, m_depth, m_height, half_w, m_depth_segments, m_height_segments);
            build_face(-axis_x, axis_y, -axis_z, m_width, m_height, half_d, m_width_segments, m_height_segments);
            build_face(axis_z, axis_y, -axis_x, m_depth, m_height, half_w, m_depth_segments, m_height_segments);
            build_face(axis_x, -axis_z, axis_y, m_width, m_depth, half_h, m_width_segments, m_depth_segments);
            build_face(axis_x, axis_z, -axis_y, m_width, m_depth, half_h, m_width_segments, m_depth_segments);

            // Tangents complete the record for tangent-aware materials
            // (standard/PBR); the position/uv/normal offsets are unchanged so
            // materials that ignore the tangent still read correctly.
            const auto tangent_vertices = assets::generate_tangents(vertices, indices);
            return assets::mesh_data::from_vertices(tangent_vertices, std::move(indices));
        }));
}
