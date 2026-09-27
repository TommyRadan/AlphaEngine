// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/materials/grid_material.hpp>

#include <string>
#include <utility>

#include <rendering_engine/gpu/types.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

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
        // binding 1, read at a dynamic offset into the per-draw ring
        // (per_draw_ubo.hpp) and matching the shadow passes' layout.
        descriptor.draw_layout.entries.push_back(per_draw_model_layout_entry());
        descriptor.frame_layout = frame_layout;

        descriptor.topology = gpu::primitive_topology::triangles;
        // The fragment stage discards off-plane pixels and writes the
        // ray-marched depth itself, neither of which a vertex-only depth
        // pipeline can reproduce, so the depth pre-pass never draws the
        // grid (even should a caller make it opaque).
        descriptor.depth_prepass = false;
        return std::make_shared<material_template>(device, std::move(descriptor));
    }

    grid_material::grid_material(std::shared_ptr<material_template> tmpl) : material(std::move(tmpl), grid_params()) {}

    grid_material::~grid_material() = default;
} // namespace rendering_engine
