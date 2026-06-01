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
#include <vector>

#include <infrastructure/math/math.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    struct material;

    // A point cloud rendered as a point-list draw, mirroring
    // @c THREE.Points: a flat list of positions submitted as individual
    // GPU points sized and coloured by a @ref points_material. Each
    // point is a non-indexed primitive, so the renderable records a
    // vertex count and leaves the index buffer empty; the scene pass
    // dispatches @c draw(vertex_count).
    struct points : public renderable
    {
        explicit points(material* mat);
        ~points() override;

        rendering_engine::util::transform transform;

        // Supply the point positions. Stored on the CPU; the GPU vertex
        // buffer is (re)created in @ref upload. Call before @ref upload.
        void set_positions(const std::vector<infrastructure::math::vec3>& positions);

        void upload() final;
        void collect_draw_items(std::vector<draw_item>& out) final;

    private:
        material* m_material{nullptr};
        std::vector<infrastructure::math::vec3> m_positions;
        uint32_t m_vertex_count{0};
        uint32_t m_vertex_stride{0};

        gpu::buffer m_vertex_buffer{};
        gpu::buffer m_draw_ubo{};
        gpu::bind_group m_draw_bind_group{};
    };
} // namespace rendering_engine
