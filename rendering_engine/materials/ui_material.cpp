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

#include <utility>

namespace
{
    // Overlay pass disables depth testing entirely so 2D elements
    // draw in submission order; depth writes also stay off so the
    // overlay never trashes the scene's depth buffer. Double-sided
    // so panes are visible regardless of winding.
    rendering_engine::material_params ui_params()
    {
        rendering_engine::material_params params{};
        params.transparent = true;
        params.blending = rendering_engine::blend_mode::normal;
        params.double_sided = true;
        params.depth_test = false;
        params.depth_write = false;
        return params;
    }
} // namespace

namespace rendering_engine
{
    std::shared_ptr<material_template> ui_material::create_template(gpu::device& device)
    {
        material_template_descriptor descriptor{};
        descriptor.name = "ui";
        descriptor.vertex_shader = gpu::shader_variant{"materials/ui.vert.glsl"};
        descriptor.fragment_shader = gpu::shader_variant{"materials/ui.frag.glsl"};

        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = 0; // panes use vertex_position_uv = 20 bytes; supplied per draw
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        vertex_layout.attributes.push_back({1, 2, gpu::scalar_type::float32, sizeof(float) * 3});
        descriptor.vertex_layouts.push_back(vertex_layout);
        descriptor.required_vertex_format = vertex_format::position_uv;
        descriptor.vertex_format_without_tangents = vertex_format::position_uv;

        // Per-draw layout: UBO with the {useTexture, color} pair at
        // binding=0; sampler at binding=1. UBOs and samplers live in
        // disjoint binding namespaces in OpenGL, but Vulkan treats
        // them as one descriptor set so the binding numbers must
        // still differ — they do. No per-frame layout, so this is
        // slot 0.
        descriptor.draw_layout.entries.push_back({0, gpu::binding_kind::uniform_buffer});
        descriptor.draw_layout.entries.push_back({1, gpu::binding_kind::texture});

        return std::make_shared<material_template>(device, std::move(descriptor));
    }

    ui_material::ui_material(std::shared_ptr<material_template> tmpl) : material(std::move(tmpl), ui_params()) {}
} // namespace rendering_engine
