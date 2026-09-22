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
