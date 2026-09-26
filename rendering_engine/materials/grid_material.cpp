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

#include <rendering_engine/materials/grid_material.hpp>

#include <string>
#include <utility>

#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace
{
    // Transparent so the grid fades; depth tested but not written, so
    // scene geometry occludes the grid while the grid never clobbers
    // the depth buffer. Double-sided so the fullscreen triangle is
    // never culled.
    rendering_engine::material_params grid_params()
    {
        rendering_engine::material_params params{};
        params.transparent = true;
        params.blending = rendering_engine::blend_mode::normal;
        params.double_sided = true;
        params.depth_test = true;
        params.depth_write = false;
        return params;
    }
} // namespace

namespace rendering_engine
{
    std::shared_ptr<material_template>
    grid_material::create_template(gpu::device& device, gpu::bind_group_layout frame_layout, float fade_distance)
    {
        material_template_descriptor descriptor{};
        descriptor.name = "grid";
        // The vertex stage unprojects the fullscreen triangle's near / far
        // plane points for the fragment ray-march; the fragment stage takes
        // its fade radius as a GRID_FADE_DISTANCE define shared by every
        // variant of this template.
        descriptor.vertex_shader = gpu::shader_variant{"materials/grid.vert.glsl"};
        descriptor.fragment_shader = gpu::shader_variant{"materials/grid.frag.glsl"};
        descriptor.fragment_shader.defines.emplace_back("GRID_FADE_DISTANCE", std::to_string(fade_distance));

        gpu::vertex_buffer_layout vertex_layout{};
        // Stride supplied per draw by the renderable; one vec3 position.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        descriptor.vertex_layouts.push_back(vertex_layout);
        descriptor.required_vertex_format = vertex_format::position;
        descriptor.vertex_format_without_tangents = vertex_format::position;

        // Per-draw layout (slot 1): the model + normal matrix UBO at
        // binding 1, matching the renderable's bind group.
        descriptor.draw_layout.entries.push_back(
            {gpu::shader_bindings::per_draw_model, gpu::binding_kind::uniform_buffer});
        descriptor.frame_layout = frame_layout;

        descriptor.topology = gpu::primitive_topology::triangles;
        return std::make_shared<material_template>(device, std::move(descriptor));
    }

    grid_material::grid_material(std::shared_ptr<material_template> tmpl) : material(std::move(tmpl), grid_params()) {}

    grid_material::~grid_material() = default;
} // namespace rendering_engine
