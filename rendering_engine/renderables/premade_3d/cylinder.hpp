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

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct material;

    // Cylinder (or truncated cone) primitive matching Three.js
    // CylinderGeometry. Centered at the origin with its axis along +Y;
    // the height spans [-height/2, +height/2]. The torso (side wall) is
    // tessellated into radial_segments columns by height_segments rows
    // of quads with smooth radial normals derived from the slope between
    // the two radii. Unless open_ended, a top and a bottom cap fan are
    // appended with flat normals. A cap whose radius is 0 is skipped, so
    // a top radius of 0 yields a cone. Vertex format is position + uv +
    // normal with CCW outward winding.
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

        rendering_engine::util::transform transform;

        void upload() override;
        void collect_draw_items(std::vector<draw_item>& out) override;

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

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_index_buffer{};
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};
    };
} // namespace rendering_engine
