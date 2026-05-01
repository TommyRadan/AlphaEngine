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
    // Cubed sphere ("quad sphere"): six face grids of NxN quads, each vertex
    // pushed out to the unit sphere. Eliminates the pole pinch of a UV sphere
    // — there is no preferred axis, only six face patches that meet at the
    // cube's corners with bounded distortion (~1.4x at corners). Pairs
    // naturally with cube-map textures, which are sampled by the surface
    // normal and are also free of the polar singularity.
    //
    // Vertex layout is the same `vertex_position_uv_normal` used by the UV
    // sphere — UVs here are face-local [0,1] coords, but most cube-map
    // workflows ignore them and sample with the world-space normal instead.
    struct cubed_sphere : public renderable
    {
        // @p subdivisions is the per-face grid resolution (NxN quads). Total
        // mesh has 6*subdivisions*subdivisions quads = 12*N*N triangles.
        cubed_sphere(unsigned int subdivisions = 32);

        rendering_engine::util::transform transform;

        void upload() final;
        void render() final;

        const opengl::vertex_array& get_vao() const;
        unsigned int get_index_count() const;

    private:
        unsigned int m_subdivisions;
        unsigned int m_index_count;

        opengl::vertex_array* m_vertex_array_object;
        opengl::vertex_buffer* m_vertex_buffer_object;
        opengl::vertex_buffer* m_index_buffer_object;
    };
} // namespace rendering_engine
