// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/cylinder.hpp>

#include <cmath>
#include <string>
#include <vector>

#include <assets/tangent.hpp>
#include <assets/vertex.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <rendering_engine/resources/cache_key.hpp>

rendering_engine::cylinder::cylinder(asset_cache& cache,
                                     material* mat,
                                     float radius_top,
                                     float radius_bottom,
                                     float height,
                                     unsigned int radial_segments,
                                     unsigned int height_segments,
                                     bool open_ended)
    : mesh_source{mat, "cylinder"}, m_radius_top{radius_top}, m_radius_bottom{radius_bottom}, m_height{height},
      m_radial_segments{radial_segments}, m_height_segments{height_segments}, m_open_ended{open_ended}
{
    fetch_mesh(cache);
}

void rendering_engine::cylinder::fetch_mesh(asset_cache& cache)
{
    // Build and upload through the asset cache, keyed by every geometry
    // parameter so two cylinders with identical parameters share one upload.
    // The builder only runs on a cache miss.
    set_mesh(cache.get_or_create_mesh(
        "cylinder:" + cache_key_number(m_radius_top) + ":" + cache_key_number(m_radius_bottom) + ":" +
            cache_key_number(m_height) + ":" + cache_key_number(m_radial_segments) + ":" +
            cache_key_number(m_height_segments) + ":" + cache_key_number(m_open_ended) + ":" +
            assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
        [this]
        {
            constexpr float pi = 3.14159265358979323846f;

            std::vector<assets::vertex_position_uv_normal> vertices;
            std::vector<uint32_t> indices;

            const float half_height = m_height * 0.5f;

            // Torso (side wall). Each row of vertices spans radial_segments + 1
            // columns so the seam UV reaches 1.0; the radius is interpolated
            // linearly between the bottom and top. The normal follows the cone
            // slope: its radial component is unit length in the xz plane and the
            // y component is the slope of the side wall.
            const float slope = (m_radius_bottom - m_radius_top) / m_height;

            for (unsigned int y = 0; y <= m_height_segments; ++y)
            {
                const float v = static_cast<float>(y) / static_cast<float>(m_height_segments);
                const float radius = v * (m_radius_top - m_radius_bottom) + m_radius_bottom;

                for (unsigned int x = 0; x <= m_radial_segments; ++x)
                {
                    const float u = static_cast<float>(x) / static_cast<float>(m_radial_segments);
                    const float theta = u * 2.0f * pi;
                    const float sin_theta = std::sin(theta);
                    const float cos_theta = std::cos(theta);

                    assets::vertex_position_uv_normal vertex;
                    vertex.pos = core::math::vec3{radius * sin_theta, -v * m_height + half_height, radius * cos_theta};
                    vertex.normal = core::math::normalize(core::math::vec3{sin_theta, slope, cos_theta});
                    vertex.uv = core::math::vec2{u, 1.0f - v};
                    vertices.push_back(vertex);
                }
            }

            const unsigned int torso_columns = m_radial_segments + 1;
            for (unsigned int y = 0; y < m_height_segments; ++y)
            {
                for (unsigned int x = 0; x < m_radial_segments; ++x)
                {
                    const uint32_t a = y * torso_columns + x;
                    const uint32_t b = (y + 1) * torso_columns + x;
                    const uint32_t c = (y + 1) * torso_columns + (x + 1);
                    const uint32_t d = y * torso_columns + (x + 1);

                    // CCW winding when viewed from outside the side wall.
                    indices.push_back(a);
                    indices.push_back(b);
                    indices.push_back(d);

                    indices.push_back(b);
                    indices.push_back(c);
                    indices.push_back(d);
                }
            }

            // Caps. A cap whose radius is 0 is skipped, so a top radius of 0
            // collapses the top into the apex of a cone. Each cap is a triangle
            // fan around a dedicated center vertex with a flat (+/-Y) normal.
            const auto generate_cap = [&](bool top)
            {
                const float radius = top ? m_radius_top : m_radius_bottom;
                if (radius <= 0.0f)
                {
                    return;
                }

                const float sign = top ? 1.0f : -1.0f;
                const float cap_y = half_height * sign;
                const core::math::vec3 normal{0.0f, sign, 0.0f};

                const uint32_t center_start = static_cast<uint32_t>(vertices.size());

                // A center vertex per radial segment keeps the cap UVs aligned
                // with the rim vertices.
                for (unsigned int x = 0; x < m_radial_segments; ++x)
                {
                    assets::vertex_position_uv_normal vertex;
                    vertex.pos = core::math::vec3{0.0f, cap_y, 0.0f};
                    vertex.normal = normal;
                    vertex.uv = core::math::vec2{0.5f, 0.5f};
                    vertices.push_back(vertex);
                }

                const uint32_t rim_start = static_cast<uint32_t>(vertices.size());
                for (unsigned int x = 0; x <= m_radial_segments; ++x)
                {
                    const float u = static_cast<float>(x) / static_cast<float>(m_radial_segments);
                    const float theta = u * 2.0f * pi;
                    const float sin_theta = std::sin(theta);
                    const float cos_theta = std::cos(theta);

                    assets::vertex_position_uv_normal vertex;
                    vertex.pos = core::math::vec3{radius * sin_theta, cap_y, radius * cos_theta};
                    vertex.normal = normal;
                    vertex.uv = core::math::vec2{cos_theta * 0.5f + 0.5f, sin_theta * 0.5f * sign + 0.5f};
                    vertices.push_back(vertex);
                }

                for (unsigned int x = 0; x < m_radial_segments; ++x)
                {
                    const uint32_t center = center_start + x;
                    const uint32_t r0 = rim_start + x;
                    const uint32_t r1 = rim_start + x + 1;

                    // Wind so the cap normal (+/-Y) points outward in CCW order.
                    if (top)
                    {
                        indices.push_back(r0);
                        indices.push_back(r1);
                        indices.push_back(center);
                    }
                    else
                    {
                        indices.push_back(r1);
                        indices.push_back(r0);
                        indices.push_back(center);
                    }
                }
            };

            if (!m_open_ended)
            {
                generate_cap(true);
                generate_cap(false);
            }

            // Tangents complete the record for tangent-aware materials
            // (standard/PBR); the position/uv/normal offsets are unchanged so
            // materials that ignore the tangent still read correctly.
            const auto tangent_vertices = assets::generate_tangents(vertices, indices);
            return assets::mesh_data::from_vertices(tangent_vertices, std::move(indices));
        }));
}
