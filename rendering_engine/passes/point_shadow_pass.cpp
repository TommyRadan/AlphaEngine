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

#include <rendering_engine/passes/point_shadow_pass.hpp>

#include <string>

#include <control/engine.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace
{
    namespace math = infrastructure::math;

    // Per-face resolution. Smaller than the 2048 directional map because six
    // faces are kept resident; 1024 is plenty for the demo's small bodies.
    constexpr uint32_t shadow_map_size = 1024;

    // Perspective frustum per face: 90 degrees covers exactly one cube face.
    // Near/far bracket the bodies around the light; a tight far keeps the
    // perspective depth precise enough to avoid acne.
    constexpr float light_near = 0.1f;
    constexpr float light_far = 20.0f;
    constexpr float shadow_bias = 0.0025f;

    constexpr uint32_t light_frame_binding = 0;
    constexpr uint32_t draw_model_binding = 1;

    // Look direction and up for each of the six faces, in the selection order
    // +X, -X, +Y, -Y, +Z, -Z. The ups only need to keep look_at well-defined;
    // the lit shader reconstructs the same matrix per face, so any consistent
    // choice works.
    struct face_basis
    {
        math::vec3 dir;
        math::vec3 up;
    };

    constexpr std::array<face_basis, 6> face_bases = {{
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
        {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
    }};

    const std::string vertex_shader = R"vs(
        #version 450

        layout(location = 0) in vec3 position;

        layout(set = 0, binding = 0, std140) uniform LightFrame
        {
            mat4 lightViewProj;
        } u_light;

        layout(set = 1, binding = 1, std140) uniform PerDraw
        {
            mat4 modelMatrix;
        } u_draw;

        void main()
        {
            gl_Position = u_light.lightViewProj * u_draw.modelMatrix * vec4(position, 1.0);
        }
)vs";

    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) out vec4 fragColor;

        void main()
        {
            fragColor = vec4(1.0);
        }
)fs";
} // namespace

namespace rendering_engine
{
    point_shadow_pass::point_shadow_pass(std::vector<renderable*>* registry) : m_registry(registry)
    {
        auto& gpu = *control::current_engine().gpu;

        for (int face = 0; face < point_shadow_face_count; ++face)
        {
            gpu::render_target_descriptor target_descriptor{};
            target_descriptor.color_format = gpu::texture_format::r8_unorm;
            target_descriptor.width = shadow_map_size;
            target_descriptor.height = shadow_map_size;
            target_descriptor.with_depth = true;
            target_descriptor.depth_format = gpu::texture_format::depth32_float;
            m_targets[face] = gpu.create_render_target(target_descriptor);
            m_depth_textures[face] = gpu.render_target_depth_texture(m_targets[face]);
        }

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_glsl_to_spirv(vertex_shader, gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.spirv = gpu::compile_glsl_to_spirv(fragment_shader, gpu::shader_stage::fragment);
        m_fragment_shader = gpu.create_shader_module(fs_descriptor);

        gpu::bind_group_layout_descriptor light_layout{};
        light_layout.entries.push_back({light_frame_binding, gpu::binding_kind::uniform_buffer});
        m_light_layout = gpu.create_bind_group_layout(light_layout);

        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({draw_model_binding, gpu::binding_kind::uniform_buffer});
        m_draw_layout = gpu.create_bind_group_layout(draw_layout);

        for (int face = 0; face < point_shadow_face_count; ++face)
        {
            gpu::buffer_descriptor ubo_descriptor{};
            ubo_descriptor.size = sizeof(math::mat4);
            ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
            ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
            m_light_ubos[face] = gpu.create_buffer(ubo_descriptor);

            gpu::bind_group_descriptor light_bind_group_descriptor{};
            light_bind_group_descriptor.layout = m_light_layout;
            gpu::binding_value light_slot{};
            light_slot.binding = light_frame_binding;
            light_slot.kind = gpu::binding_kind::uniform_buffer;
            light_slot.buffer_value = m_light_ubos[face];
            light_bind_group_descriptor.entries.push_back(light_slot);
            m_light_bind_groups[face] = gpu.create_bind_group(light_bind_group_descriptor);
        }

        gpu::vertex_buffer_layout vertex_layout{};
        vertex_layout.stride = 0;
        vertex_layout.attributes.push_back({0, 3, gpu::scalar_type::float32, 0});

        gpu::depth_state depth{};
        depth.test_enabled = true;
        depth.write_enabled = true;
        depth.compare = gpu::compare_function::less;

        gpu::blend_state blend{};
        blend.enabled = false;

        gpu::rasterizer_state rasterizer{};
        // Cull back faces (keep light-facing front faces). Critical here: the
        // caster point light sits at the centre of the emissive sun sphere, so
        // the sun's outward surface is back-facing as seen from the light and
        // is culled — otherwise the sun would occlude the entire scene in every
        // face of its own shadow map. Ordinary occluders (planets, moons) cast
        // via their light-facing front faces. Assumes outward-CCW winding.
        rasterizer.cull = gpu::cull_mode::back;
        rasterizer.front = gpu::front_face::counter_clockwise;
        rasterizer.polygon = gpu::polygon_mode::fill;

        gpu::pipeline_descriptor pipeline_descriptor{};
        pipeline_descriptor.vertex_shader = m_vertex_shader;
        pipeline_descriptor.fragment_shader = m_fragment_shader;
        pipeline_descriptor.vertex_buffers.push_back(vertex_layout);
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        pipeline_descriptor.bind_group_layouts.push_back(m_light_layout);
        pipeline_descriptor.bind_group_layouts.push_back(m_draw_layout);
        m_pipeline = gpu.create_pipeline(pipeline_descriptor);
    }

    point_shadow_pass::~point_shadow_pass()
    {
        auto& gpu = *control::current_engine().gpu;
        if (m_pipeline.valid())
        {
            gpu.destroy(m_pipeline);
            m_pipeline = {};
        }
        for (auto& bind_group : m_light_bind_groups)
        {
            if (bind_group.valid())
            {
                gpu.destroy(bind_group);
                bind_group = {};
            }
        }
        for (auto& ubo : m_light_ubos)
        {
            if (ubo.valid())
            {
                gpu.destroy(ubo);
                ubo = {};
            }
        }
        if (m_draw_layout.valid())
        {
            gpu.destroy(m_draw_layout);
            m_draw_layout = {};
        }
        if (m_light_layout.valid())
        {
            gpu.destroy(m_light_layout);
            m_light_layout = {};
        }
        if (m_fragment_shader.valid())
        {
            gpu.destroy(m_fragment_shader);
            m_fragment_shader = {};
        }
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
        for (int face = 0; face < point_shadow_face_count; ++face)
        {
            if (m_targets[face].valid())
            {
                gpu.destroy(m_targets[face]);
                m_targets[face] = {};
                m_depth_textures[face] = {};
            }
        }
    }

    gpu::texture point_shadow_pass::shadow_map(int face) const
    {
        return m_depth_textures[face];
    }

    const infrastructure::math::mat4& point_shadow_pass::light_view_projection(int face) const
    {
        return m_light_view_projections[face];
    }

    const infrastructure::math::vec3& point_shadow_pass::light_position() const
    {
        return m_light_position;
    }

    bool point_shadow_pass::has_shadow() const
    {
        return m_has_shadow;
    }

    int point_shadow_pass::shadow_point_index() const
    {
        return m_shadow_point_index;
    }

    float point_shadow_pass::depth_bias() const
    {
        return shadow_bias;
    }

    void point_shadow_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        auto& gpu = *control::current_engine().gpu;

        // Locate the first shadow-casting point light, tracking its index in
        // the packed point array so the lit shader can match it.
        const point_light* caster = nullptr;
        m_shadow_point_index = -1;
        int point_index = 0;
        for (const light* l : registered_lights())
        {
            if (l->type() != light_type::point)
            {
                continue;
            }
            if (point_index >= static_cast<int>(max_point_lights))
            {
                break;
            }
            const auto* pl = static_cast<const point_light*>(l);
            if (pl->cast_shadow)
            {
                caster = pl;
                m_shadow_point_index = point_index;
                break;
            }
            ++point_index;
        }

        m_has_shadow = caster != nullptr;
        if (m_has_shadow)
        {
            m_light_position = caster->position;
        }

        // 90-degree vertical FOV (pi/2), square aspect: exactly one cube face.
        constexpr float face_fov_y = 1.57079633f;
        const math::mat4 projection = math::perspective(face_fov_y, 1.0f, light_near, light_far);

        // Refresh and render each face. Faces are always cleared (even with no
        // caster) so the lit shader keys off has_shadow, not stale depth.
        for (int face = 0; face < point_shadow_face_count; ++face)
        {
            if (m_has_shadow)
            {
                const math::vec3 eye = m_light_position;
                const math::mat4 view = math::look_at(eye, eye + face_bases[face].dir, face_bases[face].up);
                m_light_view_projections[face] = projection * view;
                gpu.write_buffer(m_light_ubos[face], m_light_view_projections[face].data(), sizeof(math::mat4), 0);
            }

            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_targets[face];
            descriptor.color.load = gpu::load_op::clear;
            descriptor.color.clear_color = {1.0f, 1.0f, 1.0f, 1.0f};
            descriptor.use_depth = true;
            descriptor.depth.load = gpu::load_op::clear;
            descriptor.depth.clear_depth = 1.0f;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            if (!m_has_shadow)
            {
                pass_encoder->end();
                continue;
            }

            pass_encoder->set_pipeline(m_pipeline);
            pass_encoder->set_bind_group(0, m_light_bind_groups[face]);

            m_items.clear();
            for (auto* r : *m_registry)
            {
                r->collect_draw_items(m_items);
            }

            for (const auto& item : m_items)
            {
                pass_encoder->set_bind_group(1, item.per_draw_bind_group);
                pass_encoder->set_vertex_buffer(0, item.vertex_buffer, 0, item.vertex_stride);
                if (item.index_buffer.valid())
                {
                    pass_encoder->set_index_buffer(item.index_buffer, item.index_format);
                    pass_encoder->draw_indexed(item.index_count, 0);
                }
                else
                {
                    pass_encoder->draw(item.vertex_count, 0);
                }
            }

            pass_encoder->end();
        }
    }
} // namespace rendering_engine
