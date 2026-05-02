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

/**
 * @file shader.hpp
 * @brief Shader-module descriptor and the vertex-input layout used by
 *        @ref pipeline_descriptor.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct shader_module_descriptor
        {
            shader_stage stage{shader_stage::vertex};

            // Source text, in whatever language the active backend
            // accepts. The OpenGL backend takes GLSL #version 330 source
            // verbatim. A future cross-backend pipeline (SPIR-V cross,
            // glslang) would replace this string with a byte blob, but
            // the descriptor shape stays the same.
            std::string source;
        };

        // One attribute fed into the vertex shader. @ref location is the
        // shader-side input slot (matches GLSL @c layout(location=N)).
        // @ref components is the per-vertex element count (e.g. 3 for a
        // @c vec3 position) and @ref type the scalar type of each
        // component. @ref offset is the byte offset within the vertex
        // record.
        struct vertex_attribute
        {
            uint32_t location{0};
            uint32_t components{0};
            scalar_type type{scalar_type::float32};
            uint32_t offset{0};
        };

        // Layout of one vertex buffer slot fed into a pipeline. The
        // pipeline references its layouts by index; the runtime
        // @c set_vertex_buffer call binds a buffer to the same slot.
        struct vertex_buffer_layout
        {
            uint32_t stride{0};
            std::vector<vertex_attribute> attributes;
        };
    } // namespace gpu
} // namespace rendering_engine
