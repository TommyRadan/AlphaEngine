// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderables/vertex_format_check.hpp>

#include <cassert>

#include <core/log.hpp>
#include <rendering_engine/assets/mesh_asset.hpp>
#include <rendering_engine/materials/material.hpp>

namespace rendering_engine
{
    bool validate_vertex_format(
        const material& mat, vertex_format format, uint32_t vertex_stride, const char* renderable_name, bool& reported)
    {
        const vertex_format required = mat.required_vertex_format();
        const uint32_t min_stride = mat.min_vertex_stride();

        const bool stride_ok = vertex_stride >= min_stride;
        const bool format_ok = format == vertex_format::custom || required == vertex_format::custom ||
                               vertex_format_compatible(format, required);
        if (stride_ok && format_ok)
        {
            return true;
        }

        if (!reported)
        {
            reported = true;
            LOG_ERR("%s: vertex format %s (stride %u) cannot feed a material reading %s (needs stride >= %u); "
                    "skipping draw",
                    renderable_name,
                    vertex_format_name(format),
                    vertex_stride,
                    vertex_format_name(required),
                    min_stride);
        }
        assert(false && "mesh vertex format does not match the material's vertex layout");
        return false;
    }

    bool
    validate_vertex_format(const material& mat, const mesh_asset& mesh, const char* renderable_name, bool& reported)
    {
        return validate_vertex_format(mat, mesh.format, mesh.vertex_stride, renderable_name, reported);
    }
} // namespace rendering_engine
