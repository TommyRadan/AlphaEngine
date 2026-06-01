/**
 * Copyright (c) 2015-2026 Tomislav Radanovic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <rendering_engine/renderables/premade_3d/polyhedron.hpp>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <control/engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace
{
    constexpr float pi = 3.14159265358979323846f;

    // Spherical UV from a unit-length position, matching three.js
    // PolyhedronGeometry: azimuth = atan2(z, -x), inclination = acos(-y).
    // u = azimuth / (2*pi) + 0.5, v = inclination / pi + 0.5.
    infrastructure::math::vec2 spherical_uv(const infrastructure::math::vec3& dir)
    {
        const float azimuth = std::atan2(dir.z, -dir.x);
        const float inclination = std::acos(-dir.y);
        return infrastructure::math::vec2{azimuth / (2.0f * pi) + 0.5f, inclination / pi + 0.5f};
    }
} // namespace

namespace rendering_engine
{
    polyhedron::polyhedron(material* mat,
                           std::vector<float> base_vertices,
                           std::vector<uint32_t> base_indices,
                           float radius,
                           unsigned int detail)
        : m_material{mat}, m_base_vertices{std::move(base_vertices)}, m_base_indices{std::move(base_indices)},
          m_radius{radius}, m_detail{detail}
    {
    }

    polyhedron::~polyhedron()
    {
        auto& gpu = *control::current_engine().gpu;
        if (m_draw_bind_group.valid())
        {
            gpu.destroy(m_draw_bind_group);
            m_draw_bind_group = {};
        }
        if (m_draw_ubo.valid())
        {
            gpu.destroy(m_draw_ubo);
            m_draw_ubo = {};
        }
        if (m_index_buffer.valid())
        {
            gpu.destroy(m_index_buffer);
            m_index_buffer = {};
        }
        if (m_vertex_buffer.valid())
        {
            gpu.destroy(m_vertex_buffer);
            m_vertex_buffer = {};
        }
    }

    void polyhedron::upload()
    {
        using infrastructure::math::vec2;
        using infrastructure::math::vec3;

        // Pull the base direction (unit) for a base vertex index.
        const auto base_dir = [this](uint32_t i) -> vec3
        {
            const vec3 v{m_base_vertices[i * 3 + 0], m_base_vertices[i * 3 + 1], m_base_vertices[i * 3 + 2]};
            return infrastructure::math::normalize(v);
        };

        // Subdivision count per edge: 2^detail segments.
        const unsigned int cols = 1u << m_detail;

        std::vector<vertex_position_uv_normal> vertices;
        std::vector<uint32_t> indices;

        // Each base triangle is subdivided into a triangular grid. We build a
        // fresh (non-indexed) vertex list so per-triangle UV seam corrections
        // do not bleed across shared edges, then emit sequential indices. This
        // mirrors three.js, which produces a non-indexed buffer geometry.
        for (size_t f = 0; f < m_base_indices.size(); f += 3)
        {
            const vec3 a = base_dir(m_base_indices[f + 0]);
            const vec3 b = base_dir(m_base_indices[f + 1]);
            const vec3 c = base_dir(m_base_indices[f + 2]);

            // Build a grid of unit-length directions over the triangle (a, b, c)
            // using slerp-equivalent linear interpolation + renormalize, matching
            // three.js subdivision. v[i][j] for i in [0, cols], j in [0, i].
            std::vector<std::vector<vec3>> grid(cols + 1);
            for (unsigned int i = 0; i <= cols; ++i)
            {
                const vec3 ai =
                    infrastructure::math::normalize(a + (c - a) * (static_cast<float>(i) / static_cast<float>(cols)));
                const vec3 bi =
                    infrastructure::math::normalize(b + (c - b) * (static_cast<float>(i) / static_cast<float>(cols)));

                const unsigned int rows = cols - i;
                grid[i].resize(rows + 1);
                for (unsigned int j = 0; j <= rows; ++j)
                {
                    if (j == 0 && i == cols)
                    {
                        grid[i][j] = ai;
                    }
                    else
                    {
                        grid[i][j] = infrastructure::math::normalize(
                            ai + (bi - ai) * (static_cast<float>(j) / static_cast<float>(rows)));
                    }
                }
            }

            // Emit triangles from the grid (two orientations per cell), matching
            // three.js winding so the outward normal faces away from the centre.
            const auto push_vertex = [&](const vec3& dir)
            {
                vertex_position_uv_normal vertex;
                vertex.pos = dir * m_radius;
                vertex.normal = dir;
                vertex.uv = spherical_uv(dir);
                vertices.push_back(vertex);
                indices.push_back(static_cast<uint32_t>(indices.size()));
            };

            for (unsigned int i = 0; i < cols; ++i)
            {
                for (unsigned int j = 0; j < 2 * (cols - i) - 1; ++j)
                {
                    const unsigned int k = j / 2;
                    if (j % 2 == 0)
                    {
                        push_vertex(grid[i][k + 1]);
                        push_vertex(grid[i + 1][k]);
                        push_vertex(grid[i][k]);
                    }
                    else
                    {
                        push_vertex(grid[i][k + 1]);
                        push_vertex(grid[i + 1][k + 1]);
                        push_vertex(grid[i + 1][k]);
                    }
                }
            }
        }

        // Basic seam / pole UV correction, mirroring three.js correctUVs +
        // correctPoles at a per-triangle level. Because the vertex buffer is
        // non-indexed, each triangle owns three consecutive vertices, so we can
        // fix wrap-around and pole singularities without affecting neighbours.
        for (size_t t = 0; t + 3 <= vertices.size(); t += 3)
        {
            vec2& uv0 = vertices[t + 0].uv;
            vec2& uv1 = vertices[t + 1].uv;
            vec2& uv2 = vertices[t + 2].uv;

            // Wrap-around seam: if a triangle straddles the u = 0/1 boundary the
            // azimuth values jump by ~1; nudge the small ones up by 1.
            const float max_u = std::max({uv0.x, uv1.x, uv2.x});
            const float min_u = std::min({uv0.x, uv1.x, uv2.x});
            if (max_u - min_u > 0.5f)
            {
                if (uv0.x < 0.5f)
                {
                    uv0.x += 1.0f;
                }
                if (uv1.x < 0.5f)
                {
                    uv1.x += 1.0f;
                }
                if (uv2.x < 0.5f)
                {
                    uv2.x += 1.0f;
                }
            }

            // Pole correction: a vertex sitting exactly on a pole has an
            // ill-defined azimuth; bias its u toward the average of the other
            // two so the texture does not pinch into a single column.
            const auto fix_pole = [&](vec3& pos, vec2& uv, const vec2& a, const vec2& b)
            {
                if (std::abs(pos.x) < 1e-5f && std::abs(pos.z) < 1e-5f)
                {
                    uv.x = (a.x + b.x) * 0.5f;
                }
            };
            fix_pole(vertices[t + 0].pos, uv0, uv1, uv2);
            fix_pole(vertices[t + 1].pos, uv1, uv0, uv2);
            fix_pole(vertices[t + 2].pos, uv2, uv0, uv1);
        }

        m_index_count = static_cast<unsigned int>(indices.size());
        m_vertex_stride = sizeof(vertex_position_uv_normal);

        auto& gpu = *control::current_engine().gpu;

        gpu::buffer_descriptor vertex_descriptor{};
        vertex_descriptor.size = vertices.size() * sizeof(vertex_position_uv_normal);
        vertex_descriptor.usage = gpu::buffer_usage_vertex;
        vertex_descriptor.hint = gpu::buffer_usage_hint::static_data;
        vertex_descriptor.initial_data = vertices.data();
        m_vertex_buffer = gpu.create_buffer(vertex_descriptor);

        gpu::buffer_descriptor index_descriptor{};
        index_descriptor.size = indices.size() * sizeof(uint32_t);
        index_descriptor.usage = gpu::buffer_usage_index;
        index_descriptor.hint = gpu::buffer_usage_hint::static_data;
        index_descriptor.initial_data = indices.data();
        m_index_buffer = gpu.create_buffer(index_descriptor);
    }

    void polyhedron::collect_draw_items(std::vector<draw_item>& out)
    {
        if (m_material == nullptr)
        {
            LOG_WRN("polyhedron::collect_draw_items: no material");
            return;
        }
        if (!m_vertex_buffer.valid() || !m_index_buffer.valid())
        {
            return;
        }

        auto& gpu = *control::current_engine().gpu;

        if (!m_draw_ubo.valid())
        {
            gpu::buffer_descriptor ubo_descriptor{};
            ubo_descriptor.size = sizeof(infrastructure::math::mat4);
            ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
            ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
            m_draw_ubo = gpu.create_buffer(ubo_descriptor);
        }

        if (!m_draw_bind_group.valid())
        {
            gpu::bind_group_descriptor bg_descriptor{};
            bg_descriptor.layout = m_material->per_draw_layout();
            gpu::binding_value model_slot{};
            model_slot.binding = 1;
            model_slot.kind = gpu::binding_kind::uniform_buffer;
            model_slot.buffer_value = m_draw_ubo;
            bg_descriptor.entries.push_back(model_slot);
            m_draw_bind_group = gpu.create_bind_group(bg_descriptor);
        }

        const auto model_matrix = transform.get_world_matrix();
        gpu.write_buffer(m_draw_ubo, model_matrix.data(), sizeof(infrastructure::math::mat4), 0);

        draw_item item{};
        item.mat = m_material;
        item.vertex_buffer = m_vertex_buffer;
        item.index_buffer = m_index_buffer;
        item.per_draw_bind_group = m_draw_bind_group;
        item.index_count = m_index_count;
        item.vertex_stride = m_vertex_stride;
        out.push_back(item);
    }

    gpu::buffer polyhedron::get_vertex_buffer() const
    {
        return m_vertex_buffer;
    }

    gpu::buffer polyhedron::get_index_buffer() const
    {
        return m_index_buffer;
    }

    unsigned int polyhedron::get_index_count() const
    {
        return m_index_count;
    }
} // namespace rendering_engine
