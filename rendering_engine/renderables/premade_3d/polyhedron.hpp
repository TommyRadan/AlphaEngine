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

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;

    // Generic polyhedron generator.
    // Takes a base mesh expressed as a flat list of vertex positions (x, y, z
    // triplets) and a flat list of triangle indices, subdivides each base
    // triangle @c detail times, projects every resulting vertex onto the
    // sphere of the given @c radius, and derives smooth (sphere) normals plus
    // spherical UVs.
    //
    // Vertex format is position + uv + normal. Triangles are emitted with CCW
    // outward winding, matching the convention documented in sphere.cpp. The
    // four platonic-solid wrappers (tetrahedron, octahedron, icosahedron,
    // dodecahedron) feed canonical base tables into this generator.
    struct polyhedron : public renderable
    {
        polyhedron(material* mat,
                   std::vector<float> base_vertices,
                   std::vector<uint32_t> base_indices,
                   float radius = 1.0f,
                   unsigned int detail = 0);
        ~polyhedron() override;

        rendering_engine::util::transform transform;

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

        gpu::buffer get_vertex_buffer() const;
        gpu::buffer get_index_buffer() const;
        unsigned int get_index_count() const;

    private:
        material* m_material{nullptr};
        std::vector<float> m_base_vertices;
        std::vector<uint32_t> m_base_indices;
        float m_radius;
        unsigned int m_detail;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};

        // Shared geometry from the asset cache, keyed by base tables + radius +
        // detail; freed when the last polyhedron referencing it is destroyed.
        std::shared_ptr<mesh_asset> m_mesh;
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};
    };
} // namespace rendering_engine
