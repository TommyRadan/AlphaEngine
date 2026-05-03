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

#include <rendering_engine/materials/ui_material.hpp>

#include <string>

namespace
{
    const std::string vertex_shader = R"vs(
        #version 330

        layout(location=0) in vec3 position;
        layout(location=1) in vec2 uv;

        out vec2 texCoord;

        void main()
        {
            texCoord = uv;
            gl_Position = vec4(position, 1.0);
        }
)vs";

    const std::string fragment_shader = R"fs(
        #version 330

        out vec4 fragColor;
        in vec2 texCoord;

        uniform float useTexture;
        uniform vec4 color;
        uniform sampler2D tex;

        void main()
        {
            fragColor = (useTexture != 0.0) ? texture(tex, vec2(texCoord.x, 1.0 - texCoord.y)) : color;
        }
)fs";
} // namespace

namespace rendering_engine
{
    ui_material::ui_material()
    {
        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = 0; // panes use vertex_position_uv = 20 bytes; supplied per draw
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 2, gpu::scalar_type::float32, sizeof(float) * 3});

        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({0, gpu::binding_kind::float_value, "useTexture"});
        draw_layout.entries.push_back({1, gpu::binding_kind::vec4_value, "color"});
        draw_layout.entries.push_back({2, gpu::binding_kind::texture, "tex"});

        // Overlay pass disables depth testing entirely so 2D elements
        // draw in submission order; depth writes also stay off so the
        // overlay never trashes the scene's depth buffer.
        gpu::depth_state depth{};
        depth.test_enabled = false;
        depth.write_enabled = false;
        depth.compare = gpu::compare_function::always;

        gpu::blend_state blend{};
        blend.enabled = true;
        blend.src = gpu::blend_factor::src_alpha;
        blend.dst = gpu::blend_factor::one_minus_src_alpha;

        gpu::rasterizer_state rasterizer{};
        rasterizer.cull = gpu::cull_mode::none;
        rasterizer.front = gpu::front_face::counter_clockwise;

        construct_pipeline(vertex_shader,
                           fragment_shader,
                           vertex_layout,
                           draw_layout,
                           gpu::bind_group_layout{},
                           depth,
                           blend,
                           rasterizer);
    }
} // namespace rendering_engine
