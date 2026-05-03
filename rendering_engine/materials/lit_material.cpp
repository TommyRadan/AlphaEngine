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

#include <rendering_engine/materials/lit_material.hpp>

#include <string>

namespace
{
    const std::string vertex_shader = R"vs(
        #version 330

        layout(location=0) in vec3 position;

        uniform mat4 modelMatrix;
        uniform mat4 viewMatrix;
        uniform mat4 projectionMatrix;

        void main()
        {
            mat4 MVP = projectionMatrix * viewMatrix * modelMatrix;
            gl_Position = MVP * vec4(position, 1.0);
        }
)vs";

    const std::string fragment_shader = R"fs(
        #version 330

        out vec4 fragColor;

        void main()
        {
            fragColor = vec4(1.0, 1.0, 1.0, 1.0);
        }
)fs";
} // namespace

namespace rendering_engine
{
    lit_material::lit_material(gpu::bind_group_layout frame_layout)
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride = 0 — per-renderable strides are passed at
        // @c set_vertex_buffer time. The shader only reads position,
        // so attribute offset 0 stays correct across every renderable
        // that fronts this pipeline regardless of trailing attributes.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});

        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({0, gpu::binding_kind::mat4_value, "modelMatrix"});

        gpu::depth_state depth{};
        depth.test_enabled = true;
        depth.write_enabled = true;
        depth.compare = gpu::compare_function::less;

        gpu::blend_state blend{};
        blend.enabled = true;
        blend.src = gpu::blend_factor::src_alpha;
        blend.dst = gpu::blend_factor::one_minus_src_alpha;

        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = gpu::cull_mode::back;
        rasterizer.front = gpu::front_face::counter_clockwise;

        construct_pipeline(
            vertex_shader, fragment_shader, vertex_layout, draw_layout, frame_layout, depth, blend, rasterizer);
    }
} // namespace rendering_engine
