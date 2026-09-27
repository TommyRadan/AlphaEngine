// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <vector>

#include <rendering_engine/assets/vertex.hpp>
#include <rendering_engine/gpu/shader.hpp>

namespace rendering_engine
{
    // Builds @ref vertex_position_uv_normal_tangent records from an
    // indexed @ref vertex_position_uv_normal mesh by deriving a
    // per-vertex tangent frame from the position/uv/normal channels.
    // Each triangle's UV-gradient tangent (Lengyel's method) is
    // accumulated onto its three vertices weighted by the corner angle
    // at that vertex, so a vertex's frame reflects the surface around it
    // rather than however finely its neighbourhood happens to be
    // tessellated. The sum is Gram-Schmidt orthonormalized against the
    // vertex normal and its handedness baked into @c tangent.w so the
    // shader can recover the bitangent. Degenerate triangles (zero area
    // in position or UV space) contribute nothing.
    //
    // Vertices are not split: a vertex shared across a UV seam or a
    // mirror boundary receives one averaged frame, which is wrong on
    // both sides. Meshes must duplicate such vertices before calling
    // (every premade primitive already does, since a UV discontinuity
    // needs distinct UVs and therefore distinct vertices). Premade
    // primitive builders feed their existing vertex/index pair straight
    // through this helper.
    std::vector<vertex_position_uv_normal_tangent>
    generate_tangents(const std::vector<vertex_position_uv_normal>& vertices, const std::vector<uint32_t>& indices);

    // Vertex-buffer layout matching the memory layout of
    // @ref vertex_position_uv_normal_tangent, for materials that consume
    // tangents. Locations: 0 position, 1 uv, 2 normal, 3 tangent.
    gpu::vertex_buffer_layout vertex_position_uv_normal_tangent_layout();
} // namespace rendering_engine
