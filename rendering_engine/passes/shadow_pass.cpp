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

#include <rendering_engine/passes/shadow_pass.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/lighting/directional_light.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <runtime/engine.hpp>

namespace
{
    namespace math = core::math;

    // Square shadow-map resolution. 4096 keeps the auto-fit box's texels
    // dense enough for crisp edges across a single cascade; a configurable
    // resolution (and cascades for large scenes) is the issue #146
    // follow-on.
    constexpr uint32_t shadow_map_size = 4096;

    // Fixed-box fallback used only when no camera is attached, so the
    // map still holds something sensible to clear/sample. With a camera
    // the frustum is auto-fitted to the view each frame (see
    // @ref fit_light_to_camera) — the box is centred on the world origin
    // and oriented along the light direction.
    constexpr float ortho_half_extent = 6.0f;
    constexpr float light_distance = 30.0f;
    constexpr float light_near = 1.0f;
    constexpr float light_far = 80.0f;

    // Auto-fit parameters. The light box is fitted to the camera's view
    // frustum capped to @ref shadow_distance world units of depth. This is
    // a single cascade, so the cap is the crispness/coverage trade-off: the
    // wider the camera FOV and the further the cap, the larger the fitted
    // box and the fewer texels each occluder gets. 25 keeps a moderate
    // playable area sharp; covering a large world crisply is what the
    // cascaded-shadow-map follow-on (issue #146) is for.
    // @ref caster_depth_scale pulls the light's near plane back toward the
    // light (as a multiple of the fit radius) so occluders sitting between
    // the light and the visible region still rasterize into the map.
    constexpr float shadow_distance = 25.0f;
    constexpr float caster_depth_scale = 6.0f;

    // Base depth-comparison bias; the lit shader slope-scales it by the
    // surface's angle to the light to keep grazing faces from acne
    // without detaching contact shadows.
    constexpr float shadow_bias = 0.0015f;

    // Binding numbers within the depth-only pipeline. Both are UBOs and
    // share OpenGL's global UBO namespace, so they mirror the lit
    // pipeline: the per-light view-projection takes 0 and the per-draw
    // model matrix takes 1 — the latter matching every renderable's own
    // per-draw bind group so they bind unchanged here.
    constexpr uint32_t light_frame_binding = 0;
    constexpr uint32_t draw_model_binding = 1;

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

    // Depth-only: the colour attachment exists only to keep the
    // framebuffer complete, so emit a constant. The depth the lit
    // materials sample comes from the depth attachment, written
    // automatically by the rasterizer.
    const std::string fragment_shader = R"fs(
        #version 450

        layout(location = 0) out vec4 fragColor;

        void main()
        {
            fragColor = vec4(1.0);
        }
)fs";

    // Build a directional light's view-projection auto-fitted to the
    // camera's view frustum. The frustum is capped to @ref shadow_distance
    // depth, then enclosed in its bounding sphere — a rotation-invariant,
    // constant-radius fit so the box doesn't pulse as the camera turns —
    // and the box centre is snapped to whole-texel increments so the
    // shadow edges don't crawl as the camera moves.
    math::mat4 fit_light_to_camera(const math::vec3& dir,
                                   const math::vec3& up,
                                   const math::mat4& camera_view,
                                   const math::mat4& camera_projection)
    {
        const math::mat4 inverse_view_proj = math::inverse(camera_projection * camera_view);

        // Unproject the eight NDC-cube corners to world space, capping the
        // far corners at shadow_distance. View-space depth is linear along
        // each near->far edge, so the cap is a plain world-space lerp.
        std::array<math::vec3, 8> corners{};
        std::size_t count = 0;
        for (int xi = 0; xi < 2; ++xi)
        {
            for (int yi = 0; yi < 2; ++yi)
            {
                const float x = xi == 0 ? -1.0f : 1.0f;
                const float y = yi == 0 ? -1.0f : 1.0f;

                const math::vec4 near_h = inverse_view_proj * math::vec4{x, y, -1.0f, 1.0f};
                const math::vec4 far_h = inverse_view_proj * math::vec4{x, y, 1.0f, 1.0f};
                const math::vec3 near_corner{near_h.x / near_h.w, near_h.y / near_h.w, near_h.z / near_h.w};
                const math::vec3 far_corner{far_h.x / far_h.w, far_h.y / far_h.w, far_h.z / far_h.w};

                // Camera looks down -z, so forward depth is -(view * p).z.
                const math::vec4 near_view = camera_view * math::vec4{near_corner, 1.0f};
                const math::vec4 far_view = camera_view * math::vec4{far_corner, 1.0f};
                const float near_depth = -near_view.z;
                const float far_depth = -far_view.z;
                float t = 1.0f;
                if (far_depth > near_depth)
                {
                    t = std::clamp((shadow_distance - near_depth) / (far_depth - near_depth), 0.0f, 1.0f);
                }

                corners[count++] = near_corner;
                corners[count++] = math::lerp(near_corner, far_corner, t);
            }
        }

        // Bounding sphere of the capped frustum.
        math::vec3 center{0.0f, 0.0f, 0.0f};
        for (const auto& c : corners)
        {
            center += c;
        }
        center /= static_cast<float>(corners.size());

        float radius = 0.0f;
        for (const auto& c : corners)
        {
            radius = std::max(radius, math::length(c - center));
        }
        // Quantize the radius so sub-texel size jitter doesn't shimmer.
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // Snap the centre to whole-texel increments in light space.
        const float texels_per_unit = static_cast<float>(shadow_map_size) / (radius * 2.0f);
        const math::mat4 texel_view = math::scale(math::vec3{texels_per_unit, texels_per_unit, texels_per_unit}) *
                                      math::look_at(math::vec3{0.0f, 0.0f, 0.0f}, dir, up);
        const math::mat4 texel_view_inverse = math::inverse(texel_view);
        math::vec4 center_texel = texel_view * math::vec4{center, 1.0f};
        center_texel.x = std::floor(center_texel.x);
        center_texel.y = std::floor(center_texel.y);
        const math::vec4 snapped = texel_view_inverse * center_texel;
        center = math::vec3{snapped.x, snapped.y, snapped.z};

        // Orthographic box of the sphere, with the eye pulled back toward
        // the light so off-screen casters between the light and the view
        // still render into the depth map.
        const math::vec3 eye = center - dir * (radius * caster_depth_scale);
        const math::mat4 light_view = math::look_at(eye, center, up);
        const math::mat4 light_projection =
            math::ortho(-radius, radius, -radius, radius, 0.0f, radius * (caster_depth_scale + 1.0f));
        return light_projection * light_view;
    }
} // namespace

namespace rendering_engine
{
    shadow_pass::shadow_pass(std::vector<renderable*>* registry) : m_registry(registry)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Off-screen target: a throwaway single-channel colour buffer
        // plus the sampled depth32_float attachment the lit materials
        // read. begin_render_pass clears and z-tests against the depth
        // attachment automatically.
        gpu::render_target_descriptor target_descriptor{};
        target_descriptor.color_format = gpu::texture_format::r8_unorm;
        target_descriptor.width = shadow_map_size;
        target_descriptor.height = shadow_map_size;
        target_descriptor.with_depth = true;
        target_descriptor.depth_format = gpu::texture_format::depth32_float;
        m_target = gpu.create_render_target(target_descriptor);
        m_depth_texture = gpu.render_target_depth_texture(m_target);

        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_glsl_to_spirv(vertex_shader, gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        gpu::shader_module_descriptor fs_descriptor{};
        fs_descriptor.stage = gpu::shader_stage::fragment;
        fs_descriptor.spirv = gpu::compile_glsl_to_spirv(fragment_shader, gpu::shader_stage::fragment);
        m_fragment_shader = gpu.create_shader_module(fs_descriptor);

        // Light-frame layout (slot 0): the view-projection UBO.
        gpu::bind_group_layout_descriptor light_layout{};
        light_layout.entries.push_back({light_frame_binding, gpu::binding_kind::uniform_buffer});
        m_light_layout = gpu.create_bind_group_layout(light_layout);

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // identical to the layout every 3D renderable builds its
        // per-draw bind group against, so those bind groups bind here
        // unchanged.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back({draw_model_binding, gpu::binding_kind::uniform_buffer});
        m_draw_layout = gpu.create_bind_group_layout(draw_layout);

        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = sizeof(math::mat4);
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_light_ubo = gpu.create_buffer(ubo_descriptor);

        gpu::bind_group_descriptor light_bind_group_descriptor{};
        light_bind_group_descriptor.layout = m_light_layout;
        gpu::binding_value light_slot{};
        light_slot.binding = light_frame_binding;
        light_slot.kind = gpu::binding_kind::uniform_buffer;
        light_slot.buffer_value = m_light_ubo;
        light_bind_group_descriptor.entries.push_back(light_slot);
        m_light_bind_group = gpu.create_bind_group(light_bind_group_descriptor);

        // Depth-only opaque draw: position-only vertex stream (offset 0
        // of every renderable's vertex record), depth tested and
        // written, no blend, no culling so single-sided geometry such
        // as the ground plane still occludes.
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
        rasterizer.cull = gpu::cull_mode::none;
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

    shadow_pass::~shadow_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_pipeline.valid())
        {
            gpu.destroy(m_pipeline);
            m_pipeline = {};
        }
        if (m_light_bind_group.valid())
        {
            gpu.destroy(m_light_bind_group);
            m_light_bind_group = {};
        }
        if (m_light_ubo.valid())
        {
            gpu.destroy(m_light_ubo);
            m_light_ubo = {};
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
        // The depth texture is owned by the render target, so destroying
        // the target releases both attachments.
        if (m_target.valid())
        {
            gpu.destroy(m_target);
            m_target = {};
            m_depth_texture = {};
        }
    }

    gpu::texture shadow_pass::shadow_map() const
    {
        return m_depth_texture;
    }

    const core::math::mat4& shadow_pass::light_view_projection() const
    {
        return m_light_view_projection;
    }

    bool shadow_pass::has_shadow() const
    {
        return m_has_shadow;
    }

    int shadow_pass::shadow_light_index() const
    {
        return m_shadow_light_index;
    }

    float shadow_pass::depth_bias() const
    {
        return shadow_bias;
    }

    void shadow_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Locate the first shadow-casting directional light, tracking
        // its index within the packed directional array so the lit
        // shader can match it. Lights past the UBO capacity never reach
        // the shader, so they cannot cast.
        const directional_light* caster = nullptr;
        m_shadow_light_index = -1;
        int directional_index = 0;
        for (const light* l : registered_lights())
        {
            if (l->type() != light_type::directional)
            {
                continue;
            }
            if (directional_index >= static_cast<int>(max_directional_lights))
            {
                break;
            }
            const auto* dl = static_cast<const directional_light*>(l);
            if (dl->cast_shadow)
            {
                caster = dl;
                m_shadow_light_index = directional_index;
                break;
            }
            ++directional_index;
        }

        m_has_shadow = caster != nullptr;

        // Always open the pass so the depth map is cleared even on
        // no-caster frames; the lit shader keys off has_shadow rather
        // than the (possibly stale) contents.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_target;
        descriptor.color.load = gpu::load_op::clear;
        descriptor.color.clear_color = {1.0f, 1.0f, 1.0f, 1.0f};
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::clear;
        descriptor.depth.clear_depth = 1.0f;

        auto pass_encoder = encoder.begin_render_pass(descriptor);

        if (!m_has_shadow)
        {
            pass_encoder->end();
            return;
        }

        // Build the light's orthographic view-projection. Pick an up
        // vector that is not parallel to the direction so look_at stays
        // well-defined regardless of the world's up axis.
        const math::vec3 dir = math::normalize(caster->direction);
        math::vec3 up{0.0f, 1.0f, 0.0f};
        if (std::abs(math::dot(dir, up)) > 0.99f)
        {
            up = math::vec3{0.0f, 0.0f, 1.0f};
        }

        // With a camera, auto-fit the box to the visible frustum so the
        // shadow map's texels land on what the viewer actually sees;
        // otherwise fall back to a fixed box centred on the origin.
        if (const camera* cam = camera::get_current_camera(); cam != nullptr)
        {
            m_light_view_projection =
                fit_light_to_camera(dir, up, cam->get_view_matrix(), cam->get_projection_matrix());
        }
        else
        {
            const math::vec3 eye = dir * (-light_distance);
            const math::mat4 view = math::look_at(eye, math::vec3{0.0f, 0.0f, 0.0f}, up);
            const math::mat4 projection = math::ortho(
                -ortho_half_extent, ortho_half_extent, -ortho_half_extent, ortho_half_extent, light_near, light_far);
            m_light_view_projection = projection * view;
        }

        gpu.write_buffer(m_light_ubo, m_light_view_projection.data(), sizeof(math::mat4), 0);

        pass_encoder->set_pipeline(m_pipeline);
        pass_encoder->set_bind_group(0, m_light_bind_group);

        // Every scene renderable casts. Reuse the per-draw model-matrix
        // bind group each renderable already built; the depth-only
        // pipeline reads only position so the differing vertex strides
        // are absorbed by the per-draw stride override.
        m_items.clear();
        for (auto* r : *m_registry)
        {
            if (!r->casts_shadow())
            {
                continue;
            }
            r->collect_draw_items(m_items);
        }

        for (const auto& item : m_items)
        {
            // Instanced renderables carry their transforms in a per-draw
            // storage buffer the depth-only pipeline can't consume, so
            // they don't cast shadows yet — skip them rather than emit a
            // single garbage caster from the unbound model UBO.
            if (item.indirect_buffer.valid())
            {
                continue;
            }
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
} // namespace rendering_engine
