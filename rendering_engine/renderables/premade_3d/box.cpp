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

#include <rendering_engine/renderables/premade_3d/box.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <core/log.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/mesh/tangent.hpp>
#include <rendering_engine/mesh/vertex.hpp>
#include <runtime/engine.hpp>

rendering_engine::box::box(material* mat,
                           float width,
                           float height,
                           float depth,
                           unsigned int width_segments,
                           unsigned int height_segments,
                           unsigned int depth_segments)
    : m_material{mat}, m_width{width}, m_height{height}, m_depth{depth},
      m_width_segments{width_segments < 1 ? 1 : width_segments},
      m_height_segments{height_segments < 1 ? 1 : height_segments},
      m_depth_segments{depth_segments < 1 ? 1 : depth_segments}
{
}

rendering_engine::box::~box()
{
    auto& gpu = *runtime::current_engine().gpu;
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
    // m_mesh is shared geometry owned by the asset cache; it is released by its
    // shared_ptr, not destroyed here.
}

void rendering_engine::box::upload()
{
    m_vertex_stride = sizeof(vertex_position_uv_normal_tangent);

    // Build and upload through the asset cache, keyed by dimensions and
    // segment counts so two boxes of the same geometry share one upload. The
    // builder only runs on a cache miss.
    m_mesh = runtime::current_engine().assets->get_or_create_mesh(
        "box:" + std::to_string(m_width) + "x" + std::to_string(m_height) + "x" + std::to_string(m_depth) + ":" +
            std::to_string(m_width_segments) + "x" + std::to_string(m_height_segments) + "x" +
            std::to_string(m_depth_segments),
        [this]
        {
            using core::math::vec2;
            using core::math::vec3;

            std::vector<vertex_position_uv_normal> vertices;
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

                        vertex_position_uv_normal vertex;
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

                        indices.push_back(a);
                        indices.push_back(c);
                        indices.push_back(d);

                        indices.push_back(a);
                        indices.push_back(d);
                        indices.push_back(b);
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

            const auto tangent_vertices = generate_tangents(vertices, indices);

            return mesh_data::from_vertices(tangent_vertices, std::move(indices));
        });

    m_index_count = m_mesh->index_count;
}

void rendering_engine::box::collect_draw_items(std::vector<draw_item>& out)
{
    if (m_material == nullptr)
    {
        LOG_WRN("box::collect_draw_items: no material");
        return;
    }
    if (!m_mesh)
    {
        return;
    }

    auto& gpu = *runtime::current_engine().gpu;

    if (!m_draw_ubo.valid())
    {
        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sizeof(core::math::mat4);
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
    gpu.write_buffer(m_draw_ubo, model_matrix.data(), sizeof(core::math::mat4), 0);

    draw_item item{};
    item.mat = m_material;
    item.vertex_buffer = m_mesh->vertex_buffer;
    item.index_buffer = m_mesh->index_buffer;
    item.per_draw_bind_group = m_draw_bind_group;
    item.index_count = m_index_count;
    item.vertex_stride = m_vertex_stride;
    out.push_back(item);
}

rendering_engine::gpu::buffer rendering_engine::box::get_vertex_buffer() const
{
    return m_mesh ? m_mesh->vertex_buffer : gpu::buffer{};
}

rendering_engine::gpu::buffer rendering_engine::box::get_index_buffer() const
{
    return m_mesh ? m_mesh->index_buffer : gpu::buffer{};
}

unsigned int rendering_engine::box::get_index_count() const
{
    return m_index_count;
}
