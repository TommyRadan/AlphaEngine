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

#include <rendering_engine/mesh/tangent.hpp>

#include <cmath>
#include <cstddef>

namespace rendering_engine
{
    std::vector<vertex_position_uv_normal_tangent>
    generate_tangents(const std::vector<vertex_position_uv_normal>& vertices, const std::vector<uint32_t>& indices)
    {
        namespace math = core::math;

        const std::size_t count = vertices.size();
        std::vector<math::vec3> tangent_accum(count, math::vec3{0.0f});
        std::vector<math::vec3> bitangent_accum(count, math::vec3{0.0f});

        // Accumulate the per-triangle tangent/bitangent onto each of its
        // three vertices. Solving the [edge1; edge2] = [uv1; uv2] * [T; B]
        // system gives the tangent basis aligned with the UV gradient.
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t i0 = indices[i + 0];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];

            const math::vec3& p0 = vertices[i0].pos;
            const math::vec3& p1 = vertices[i1].pos;
            const math::vec3& p2 = vertices[i2].pos;

            const math::vec2& w0 = vertices[i0].uv;
            const math::vec2& w1 = vertices[i1].uv;
            const math::vec2& w2 = vertices[i2].uv;

            const math::vec3 edge1 = p1 - p0;
            const math::vec3 edge2 = p2 - p0;

            const float du1 = w1.x - w0.x;
            const float dv1 = w1.y - w0.y;
            const float du2 = w2.x - w0.x;
            const float dv2 = w2.y - w0.y;

            const float det = du1 * dv2 - du2 * dv1;
            // Degenerate UVs (zero-area in texture space) contribute no
            // gradient; skip them rather than dividing by zero.
            if (std::fabs(det) <= 1e-12f)
            {
                continue;
            }
            const float r = 1.0f / det;

            const math::vec3 tangent = (edge1 * dv2 - edge2 * dv1) * r;
            const math::vec3 bitangent = (edge2 * du1 - edge1 * du2) * r;

            tangent_accum[i0] += tangent;
            tangent_accum[i1] += tangent;
            tangent_accum[i2] += tangent;

            bitangent_accum[i0] += bitangent;
            bitangent_accum[i1] += bitangent;
            bitangent_accum[i2] += bitangent;
        }

        std::vector<vertex_position_uv_normal_tangent> out;
        out.reserve(count);

        for (std::size_t v = 0; v < count; ++v)
        {
            const math::vec3& n = vertices[v].normal;
            const math::vec3& t = tangent_accum[v];

            // Gram-Schmidt: remove the normal-aligned component so the
            // tangent lies in the surface plane, then renormalize. Fall
            // back to a stable basis vector when accumulation produced a
            // near-zero tangent (e.g. an unreferenced vertex).
            math::vec3 ortho = t - n * math::dot(n, t);
            if (math::length(ortho) <= 1e-8f)
            {
                const math::vec3 axis =
                    std::fabs(n.x) < 0.99f ? math::vec3{1.0f, 0.0f, 0.0f} : math::vec3{0.0f, 1.0f, 0.0f};
                ortho = axis - n * math::dot(n, axis);
            }
            ortho = math::normalize(ortho);

            // Handedness: if the accumulated bitangent disagrees with the
            // right-handed cross(normal, tangent), the UV winding is
            // mirrored, so store -1 to flip the reconstructed bitangent.
            const float handedness = math::dot(math::cross(n, ortho), bitangent_accum[v]) < 0.0f ? -1.0f : 1.0f;

            vertex_position_uv_normal_tangent vertex;
            vertex.pos = vertices[v].pos;
            vertex.uv = vertices[v].uv;
            vertex.normal = vertices[v].normal;
            vertex.tangent = math::vec4{ortho, handedness};
            out.push_back(vertex);
        }

        return out;
    }

    gpu::vertex_buffer_layout vertex_position_uv_normal_tangent_layout()
    {
        gpu::vertex_buffer_layout layout{};
        layout.stride = sizeof(vertex_position_uv_normal_tangent);
        layout.attributes.push_back(
            {0, 3, gpu::scalar_type::float32, offsetof(vertex_position_uv_normal_tangent, pos)});
        layout.attributes.push_back({1, 2, gpu::scalar_type::float32, offsetof(vertex_position_uv_normal_tangent, uv)});
        layout.attributes.push_back(
            {2, 3, gpu::scalar_type::float32, offsetof(vertex_position_uv_normal_tangent, normal)});
        layout.attributes.push_back(
            {3, 4, gpu::scalar_type::float32, offsetof(vertex_position_uv_normal_tangent, tangent)});
        return layout;
    }
} // namespace rendering_engine
