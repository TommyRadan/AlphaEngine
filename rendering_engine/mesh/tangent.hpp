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

#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/mesh/vertex.hpp>

namespace rendering_engine
{
    // Builds @ref vertex_position_uv_normal_tangent records from an
    // indexed @ref vertex_position_uv_normal mesh by deriving a
    // per-vertex tangent frame from the position/uv/normal channels.
    // The tangent is accumulated over every triangle sharing a vertex
    // (Lengyel's method), Gram-Schmidt orthonormalized against the
    // vertex normal, and its handedness baked into @c tangent.w so the
    // shader can recover the bitangent. Premade primitive builders feed
    // their existing vertex/index pair straight through this helper.
    std::vector<vertex_position_uv_normal_tangent>
    generate_tangents(const std::vector<vertex_position_uv_normal>& vertices, const std::vector<uint32_t>& indices);

    // Vertex-buffer layout matching the memory layout of
    // @ref vertex_position_uv_normal_tangent, for materials that consume
    // tangents. Locations: 0 position, 1 uv, 2 normal, 3 tangent.
    gpu::vertex_buffer_layout vertex_position_uv_normal_tangent_layout();
} // namespace rendering_engine
