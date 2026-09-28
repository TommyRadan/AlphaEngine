// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>
#include <vector>

#include <assets/vertex.hpp>

namespace assets
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
    // (every shape of @ref mesh_generators already does, since a UV
    // discontinuity needs distinct UVs and therefore distinct vertices).
    // The mesh generators feed their vertex/index pair straight through
    // this helper.
    std::vector<vertex_position_uv_normal_tangent>
    generate_tangents(const std::vector<vertex_position_uv_normal>& vertices, const std::vector<uint32_t>& indices);
} // namespace assets
