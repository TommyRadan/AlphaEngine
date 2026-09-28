// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/plane.hpp>

#include <string>
#include <vector>

#include <assets/tangent.hpp>
#include <assets/vertex.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <rendering_engine/resources/cache_key.hpp>

rendering_engine::plane::plane(asset_cache& cache,
                               material* mat,
                               float width,
                               float height,
                               unsigned int width_segments,
                               unsigned int height_segments)
    : mesh_source{mat, "plane"}, m_width{width}, m_height{height}, m_width_segments{width_segments},
      m_height_segments{height_segments}
{
    fetch_mesh(cache);
}

void rendering_engine::plane::fetch_mesh(asset_cache& cache)
{
    // Build and upload through the asset cache, keyed by dimensions and segment
    // counts so two planes of the same geometry share one upload. The builder
    // only runs on a cache miss.
    set_mesh(cache.get_or_create_mesh(
        "plane:" + cache_key_number(m_width) + "x" + cache_key_number(m_height) + ":" +
            cache_key_number(m_width_segments) + "x" + cache_key_number(m_height_segments) + ":" +
            assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
        [this]
        {
            const unsigned int columns = m_width_segments + 1;
            const unsigned int rows = m_height_segments + 1;

            std::vector<assets::vertex_position_uv_normal> vertices;
            vertices.reserve(columns * rows);

            const float half_width = m_width * 0.5f;
            const float half_height = m_height * 0.5f;

            for (unsigned int i = 0; i < rows; ++i)
            {
                const float v = static_cast<float>(i) / static_cast<float>(m_height_segments);
                const float y = v * m_height - half_height;

                for (unsigned int j = 0; j < columns; ++j)
                {
                    const float u = static_cast<float>(j) / static_cast<float>(m_width_segments);
                    const float x = u * m_width - half_width;

                    assets::vertex_position_uv_normal vertex;
                    vertex.pos = core::math::vec3{x, y, 0.0f};
                    vertex.uv = core::math::vec2{u, 1.0f - v};
                    vertex.normal = core::math::vec3{0.0f, 0.0f, 1.0f};
                    vertices.push_back(vertex);
                }
            }

            std::vector<uint32_t> indices;
            indices.reserve(m_width_segments * m_height_segments * 6);

            for (unsigned int i = 0; i < m_height_segments; ++i)
            {
                for (unsigned int j = 0; j < m_width_segments; ++j)
                {
                    const uint32_t a = i * columns + j;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + columns;
                    const uint32_t d = c + 1;

                    // CCW winding when viewed from the +Z side (the front face),
                    // so the +Z normal points toward the camera looking down -Z.
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);

                    indices.push_back(a);
                    indices.push_back(d);
                    indices.push_back(c);
                }
            }

            // Tangents complete the record for tangent-aware materials
            // (standard/PBR); the position/uv/normal offsets are unchanged so
            // materials that ignore the tangent still read correctly.
            const auto tangent_vertices = assets::generate_tangents(vertices, indices);
            return assets::mesh_data::from_vertices(tangent_vertices, std::move(indices));
        }));
}
