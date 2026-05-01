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

#include <rendering_engine/renderables/renderable.hpp>

#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/util/transform.hpp>

namespace rendering_engine
{
    // UV-sphere with quad-based tessellation (each lat/lon cell is two triangles).
    // Vertex format is position + uv + normal. Texturing is well-behaved away from
    // the poles; near the poles UVs are pinched but per-vertex normals stay smooth.
    struct sphere : public renderable
    {
        sphere(unsigned int stacks = 64, unsigned int slices = 128);

        rendering_engine::util::transform transform;

        void upload() final;
        void render() final;

        // Direct accessors for callers that want to bind the VAO under a custom
        // shader program rather than going through the active renderer.
        const opengl::vertex_array& get_vao() const;
        unsigned int get_index_count() const;

    private:
        unsigned int m_stacks;
        unsigned int m_slices;
        unsigned int m_index_count;

        opengl::vertex_array* m_vertex_array_object;
        opengl::vertex_buffer* m_vertex_buffer_object;
        opengl::vertex_buffer* m_index_buffer_object;
    };
} // namespace rendering_engine
