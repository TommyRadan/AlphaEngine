// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#pragma once

#include <cstdint>

#include <rendering_engine/assets/vertex.hpp>

namespace rendering_engine
{
    struct material;
    struct mesh_asset;

    // Confirms a vertex stream of @p format and @p vertex_stride can be
    // bound to slot 0 of @p mat's pipeline before a renderable emits a
    // @c draw_item for it. Two checks: the record must be at least as wide
    // as the pipeline's furthest-reaching attribute
    // (@ref material::min_vertex_stride), so no fetch can run past the end
    // of a vertex; and, when both the mesh and the material name a
    // format, the mesh must carry the channels the material reads at the
    // offsets it expects (@ref vertex_format_compatible). A @c custom
    // format on either side skips the second check and relies on the
    // first.
    //
    // On a mismatch the failure is logged once per renderable (@p reported
    // latches so a persistent mismatch does not log every frame), asserted
    // in debug builds, and @c false is returned so the caller skips the
    // draw instead of reading out of bounds. @p renderable_name labels the
    // log line.
    bool validate_vertex_format(
        const material& mat, vertex_format format, uint32_t vertex_stride, const char* renderable_name, bool& reported);

    // Convenience overload for a cached @ref mesh_asset: checks its recorded
    // format and stride.
    bool
    validate_vertex_format(const material& mat, const mesh_asset& mesh, const char* renderable_name, bool& reported);
} // namespace rendering_engine
