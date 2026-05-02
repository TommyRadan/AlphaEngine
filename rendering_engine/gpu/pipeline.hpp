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
 * @file pipeline.hpp
 * @brief Pipeline state object — bakes shader, vertex layout, and fixed-
 *        function state at create time.
 */

#pragma once

#include <vector>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/handle.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace rendering_engine
{
    namespace gpu
    {
        struct blend_state
        {
            bool enabled{false};
            blend_factor src{blend_factor::one};
            blend_factor dst{blend_factor::zero};
            blend_op op{blend_op::add};
        };

        struct depth_state
        {
            bool test_enabled{true};
            bool write_enabled{true};
            compare_function compare{compare_function::less};
        };

        struct rasterizer_state
        {
            cull_mode cull{cull_mode::back};
            front_face front{front_face::counter_clockwise};
            polygon_mode polygon{polygon_mode::fill};
        };

        // Pipeline state object: every piece of GPU state that a
        // backend can bake at create time lives here. Bound to a
        // @c render_pass_encoder by handle; binding does not consult any
        // string table.
        struct pipeline_descriptor
        {
            shader_module vertex_shader{};
            shader_module fragment_shader{};
            shader_module geometry_shader{};

            std::vector<vertex_buffer_layout> vertex_buffers;

            primitive_topology topology{primitive_topology::triangles};
            blend_state blend;
            depth_state depth;
            rasterizer_state rasterizer;

            // Bind-group layouts referenced by this pipeline. The index
            // into this vector is the slot number passed to
            // @c render_pass_encoder::set_bind_group.
            std::vector<bind_group_layout> bind_group_layouts;
        };
    } // namespace gpu
} // namespace rendering_engine
