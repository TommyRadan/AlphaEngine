// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/premade_3d/sphere.hpp>

#include <cmath>
#include <string>
#include <vector>

#include <assets/tangent.hpp>
#include <assets/vertex.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/resources/asset_cache.hpp>

rendering_engine::sphere::sphere(asset_cache& cache, material* mat, unsigned int stacks, unsigned int slices)
    : mesh_source{mat, "sphere"}, m_stacks{stacks}, m_slices{slices}
{
    fetch_mesh(cache);
}

void rendering_engine::sphere::fetch_mesh(asset_cache& cache)
{
    // Build and upload through the asset cache, keyed by tessellation so two
    // spheres of the same resolution share one upload. The builder only runs on
    // a cache miss.
    set_mesh(cache.get_or_create_mesh("sphere:" + std::to_string(m_stacks) + "x" + std::to_string(m_slices) + ":" +
                                          assets::vertex_format_name(assets::vertex_format::position_uv_normal_tangent),
                                      [this]
                                      {
                                          const unsigned int rings = m_stacks + 1;
                                          const unsigned int columns = m_slices + 1;

                                          std::vector<assets::vertex_position_uv_normal> vertices;
                                          vertices.reserve(rings * columns);

                                          constexpr float pi = 3.14159265358979323846f;

                                          for (unsigned int i = 0; i < rings; ++i)
                                          {
                                              const float v = static_cast<float>(i) / static_cast<float>(m_stacks);
                                              const float phi = pi * v;
                                              const float sin_phi = std::sin(phi);
                                              const float cos_phi = std::cos(phi);

                                              for (unsigned int j = 0; j < columns; ++j)
                                              {
                                                  const float u = static_cast<float>(j) / static_cast<float>(m_slices);
                                                  const float theta = 2.0f * pi * u;
                                                  const float sin_theta = std::sin(theta);
                                                  const float cos_theta = std::cos(theta);

                                                  assets::vertex_position_uv_normal vertex;
                                                  vertex.pos = core::math::vec3{
                                                      sin_phi * cos_theta, sin_phi * sin_theta, cos_phi};
                                                  vertex.normal = vertex.pos;
                                                  vertex.uv = core::math::vec2{u, 1.0f - v};
                                                  vertices.push_back(vertex);
                                              }
                                          }

                                          std::vector<uint32_t> indices;
                                          indices.reserve(m_stacks * m_slices * 6);

                                          for (unsigned int i = 0; i < m_stacks; ++i)
                                          {
                                              for (unsigned int j = 0; j < m_slices; ++j)
                                              {
                                                  const uint32_t a = i * columns + j;
                                                  const uint32_t b = a + 1;
                                                  const uint32_t c = a + columns;
                                                  const uint32_t d = c + 1;

                                                  // CCW winding when viewed from outside the sphere — going
                                                  // top-left -> bottom-left -> bottom-right traces a CCW loop
                                                  // in screen space when the outward normal points toward
                                                  // the camera.
                                                  indices.push_back(a);
                                                  indices.push_back(c);
                                                  indices.push_back(d);

                                                  indices.push_back(a);
                                                  indices.push_back(d);
                                                  indices.push_back(b);
                                              }
                                          }

                                          // Tangents complete the record for tangent-aware materials
                                          // (standard/PBR); the position/uv/normal offsets are unchanged so
                                          // materials that ignore the tangent still read correctly.
                                          const auto tangent_vertices = assets::generate_tangents(vertices, indices);
                                          return assets::mesh_data::from_vertices(tangent_vertices, std::move(indices));
                                      }));
}
