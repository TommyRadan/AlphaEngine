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

#include <rendering_engine/gpu/shader_bindings.hpp>
#include <rendering_engine/gpu/types.hpp>

namespace
{
    // This material's stages, by shader-library path (see shaders/materials/).
    // The vertex stage unprojects the fullscreen triangle's near / far
    // plane points for the fragment ray-march; the fragment stage takes
    // its fade radius as a GRID_FADE_DISTANCE define, so every distinct
    // fade distance is its own pipeline variant.
    const rendering_engine::gpu::shader_variant vertex_shader{"materials/grid.vert.glsl"};

    rendering_engine::gpu::shader_variant fragment_shader(float fade_distance)
    {
        rendering_engine::gpu::shader_variant variant{"materials/grid.frag.glsl"};
        variant.defines.emplace_back("GRID_FADE_DISTANCE", std::to_string(fade_distance));
        return variant;
    }
} // namespace

namespace rendering_engine
{
    grid_material::grid_material(gpu::bind_group_layout frame_layout, float fade_distance)
    {
        gpu::vertex_buffer_layout vertex_layout{};
        // Stride supplied per draw by the renderable; one vec3 position.
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});
        m_vertex_format = vertex_format::position;

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // matching the renderable's bind group.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({gpu::shader_bindings::per_draw_model, gpu::binding_kind::uniform_buffer});

        // Transparent so the grid fades; depth tested but not written, so
        // scene geometry occludes the grid while the grid never clobbers
        // the depth buffer. Double-sided so the fullscreen triangle is
        // never culled.
        material_params params{};
        params.transparent = true;
        params.blending = blend_mode::normal;
        params.double_sided = true;
        params.depth_test = true;
        params.depth_write = false;

        construct_pipeline(vertex_shader,
                           fragment_shader(fade_distance),
                           vertex_layout,
                           draw_layout,
                           frame_layout,
                           params,
                           gpu::bind_group_layout_descriptor{},
                           gpu::primitive_topology::triangles);
    }

    grid_material::~grid_material() = default;
} // namespace rendering_engine
