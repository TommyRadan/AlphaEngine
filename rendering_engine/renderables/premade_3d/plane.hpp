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

    // Flat subdivided plane lying in the XY plane, centred on the origin.
    // Parameterised by (width, height, width_segments, height_segments).
    // Vertex format is position + uv + normal; every
    // normal points along +Z and UVs span [0, 1] across the surface.
    struct plane : public renderable
    {
        explicit plane(material* mat,
                       float width = 1.0f,
                       float height = 1.0f,
                       unsigned int width_segments = 1,
                       unsigned int height_segments = 1);
        ~plane() override;

        rendering_engine::util::transform transform;

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

        gpu::buffer get_vertex_buffer() const;
        gpu::buffer get_index_buffer() const;
        unsigned int get_index_count() const;

    private:
        material* m_material{nullptr};
        float m_width;
        float m_height;
        unsigned int m_width_segments;
        unsigned int m_height_segments;
        unsigned int m_index_count{0};
        uint32_t m_vertex_stride{0};

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_index_buffer{};
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};
    };
} // namespace rendering_engine
