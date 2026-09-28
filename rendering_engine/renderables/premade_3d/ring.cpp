// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/ring.hpp>

#include <cmath>
#include <string>
#include <vector>

#include <assets/tangent.hpp>
#include <assets/vertex.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <rendering_engine/resources/cache_key.hpp>

rendering_engine::ring::ring(asset_cache& cache,
                             material* mat,
                             float inner_radius,
                             float outer_radius,
                             unsigned int theta_segments,
                             unsigned int phi_segments,
                             float theta_start,
                             float theta_length)
    : mesh_source{mat, "ring"}, m_inner_radius{inner_radius}, m_outer_radius{outer_radius},
      m_theta_segments{theta_segments}, m_phi_segments{phi_segments}, m_theta_start{theta_start},
      m_theta_length{theta_length}
{
    fetch_mesh(cache);
}

void rendering_engine::ring::fetch_mesh(asset_cache& cache)
{
    // Build and upload through the asset cache, keyed by every geometry
    // parameter so two rings with identical parameters share one upload. The
    // builder only runs on a cache miss.
    set_mesh(cache.get_or_create_mesh(
        "ring:" + cache_key_number(m_inner_radius) + ":" + cache_key_number(m_outer_radius) + ":" +
            cache_key_number(m_theta_segments) + ":" + cache_key_number(m_phi_segments) + ":" +
            cache_key_number(m_theta_start) + ":" + cache_key_number(m_theta_length) + ":" +
            assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
        [this]
        {
            const unsigned int theta_segments = m_theta_segments < 3 ? 3 : m_theta_segments;
            const unsigned int phi_segments = m_phi_segments < 1 ? 1 : m_phi_segments;

            const unsigned int columns = theta_segments + 1;
            const unsigned int rows = phi_segments + 1;

            std::vector<assets::vertex_position_uv_normal> vertices;
            vertices.reserve(rows * columns);

            const core::math::vec3 normal{0.0f, 0.0f, 1.0f};

            // Radial/angular grid: row j sweeps the radius from inner to outer, column
            // i sweeps the angle from theta_start across theta_length.
            float radius = m_inner_radius;
            const float radius_step = (m_outer_radius - m_inner_radius) / static_cast<float>(phi_segments);

            for (unsigned int j = 0; j < rows; ++j)
            {
                for (unsigned int i = 0; i < columns; ++i)
                {
                    const float segment =
                        m_theta_start + static_cast<float>(i) / static_cast<float>(theta_segments) * m_theta_length;
                    const float x = radius * std::cos(segment);
                    const float y = radius * std::sin(segment);

                    assets::vertex_position_uv_normal vertex;
                    vertex.pos = core::math::vec3{x, y, 0.0f};
                    vertex.uv =
                        core::math::vec2{(x / m_outer_radius + 1.0f) / 2.0f, (y / m_outer_radius + 1.0f) / 2.0f};
                    vertex.normal = normal;
                    vertices.push_back(vertex);
                }

                radius += radius_step;
            }

            std::vector<uint32_t> indices;
            indices.reserve(theta_segments * phi_segments * 6);

            // Two triangles per grid cell, wound CCW when viewed from +Z so the +Z
            // normal faces the camera on the front side.
            for (unsigned int j = 0; j < phi_segments; ++j)
            {
                for (unsigned int i = 0; i < theta_segments; ++i)
                {
                    const uint32_t a = j * columns + i;
                    const uint32_t b = a + columns;
                    const uint32_t c = b + 1;
                    const uint32_t d = a + 1;

                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);

                    indices.push_back(b);
                    indices.push_back(c);
                    indices.push_back(d);
                }
            }

            // Tangents complete the record for tangent-aware materials
            // (standard/PBR); the position/uv/normal offsets are unchanged so
            // materials that ignore the tangent still read correctly.
            const auto tangent_vertices = assets::generate_tangents(vertices, indices);
            return assets::mesh_data::from_vertices(tangent_vertices, std::move(indices));
        }));
}
