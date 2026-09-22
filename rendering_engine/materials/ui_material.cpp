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
    // This material's stages, by shader-library path (see shaders/materials/).
    const rendering_engine::gpu::shader_variant vertex_shader{"materials/ui.vert.glsl"};
    const rendering_engine::gpu::shader_variant fragment_shader{"materials/ui.frag.glsl"};
} // namespace

namespace rendering_engine
{
    ui_material::ui_material()
    {
        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = 0; // panes use vertex_position_uv = 20 bytes; supplied per draw
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 2, gpu::scalar_type::float32, sizeof(float) * 3});
        m_vertex_format = vertex_format::position_uv;

        // Per-draw layout: UBO with the {useTexture, color} pair at
        // binding=0; sampler at binding=1. UBOs and samplers live in
        // disjoint binding namespaces in OpenGL, but Vulkan treats
        // them as one descriptor set so the binding numbers must
        // still differ — they do.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({0, gpu::binding_kind::uniform_buffer});
        draw_layout.entries.push_back({1, gpu::binding_kind::texture});

        // Overlay pass disables depth testing entirely so 2D elements
        // draw in submission order; depth writes also stay off so the
        // overlay never trashes the scene's depth buffer. Double-sided
        // so panes are visible regardless of winding.
        material_params params{};
        params.transparent = true;
        params.blending = blend_mode::normal;
        params.double_sided = true;
        params.depth_test = false;
        params.depth_write = false;

        construct_pipeline(
            vertex_shader, fragment_shader, vertex_layout, draw_layout, gpu::bind_group_layout{}, params);
    }
} // namespace rendering_engine
