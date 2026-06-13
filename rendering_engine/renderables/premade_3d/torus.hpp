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

#include <memory>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;

    // Torus (donut) surface.
    // @c radius is the distance from the centre of the torus to the centre
    // of the tube, @c tube is the radius of the tube itself. @c radial_segments
    // controls the tessellation around the tube cross-section, @c tubular_segments
    // the tessellation around the main ring, and @c arc sweeps a partial ring
    // (full ring at 2*pi). Each cell is two triangles. Vertex format is
    // position + uv + normal with outward-facing CCW winding.
    struct torus : public renderable
    {
        explicit torus(material* mat,
                       float radius = 1.0f,
                       float tube = 0.4f,
                       unsigned int radial_segments = 12,
                       unsigned int tubular_segments = 48,
                       float arc = 6.28318530718f /* 2*pi */);
        ~torus() override;

        rendering_engine::util::transform transform;

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

        gpu::buffer get_vertex_buffer() const;
        gpu::buffer get_index_buffer() const;
        unsigned int get_index_count() const;

    private:
        material* m_material{nullptr};
        float m_radius;
        float m_tube;
        unsigned int m_radial_segments;
        unsigned int m_tubular_segments;
        float m_arc;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};

        std::shared_ptr<mesh_asset> m_mesh;
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};
    };
} // namespace rendering_engine
