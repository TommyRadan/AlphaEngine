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

#include <rendering_engine/passes/spot_shadow_pass.hpp>

#include <algorithm>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/lighting/spot_light.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <runtime/engine.hpp>

namespace
{
    namespace math = core::math;

    // Perspective frustum: the vertical FOV is twice the caster's outer
    // cone half-angle, so the map exactly covers the cone and no texels
    // are spent outside it. Clamped away from the edges (a cone must be
    // narrower than a hemisphere for a perspective projection to make
    // sense) so a misconfigured light cannot hand look_at/perspective a
    // degenerate frustum.
    constexpr float min_outer_angle = 0.01f;
    constexpr float max_outer_angle = 1.55334303f; // just under 89 degrees

    constexpr float light_near = 0.1f;
    constexpr float default_light_far = 20.0f;
    constexpr float shadow_bias = 0.0025f;

    // Constant term of the depth-only pipeline's rasteriser depth bias,
    // matching shadow_pass: one resolvable depth step. The slope term is
    // core::shadow_settings::slope_bias times the caster's depth slope,
    // lifting grazing casters clear of their own samples without
    // detaching contact shadows.
    constexpr float shadow_depth_bias_constant = 1.0f;

    // The far plane for a caster: its range, or the default when it has
    // no cutoff. Never closer than the near plane. Mirrors
    // point_shadow_pass::face_far_plane.
    float far_plane(const rendering_engine::spot_light& caster)
    {
        const float far = caster.range > 0.0f ? caster.range : default_light_far;
        return far > light_near ? far : default_light_far;
    }

    constexpr uint32_t light_frame_binding = 0;
} // namespace

namespace rendering_engine
{
    spot_shadow_pass::spot_shadow_pass(std::vector<renderable*>* registry, const core::shadow_settings& settings)
        : m_registry(registry)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Square map of the configured shadow resolution (the same edge
        // as each directional cascade): a single map, unlike the six the
        // omni pass keeps resident, so it affords the full size. Clamped
        // to the device's 2D limit.
        uint32_t shadow_map_size = std::max(settings.resolution, 1u);
        if (const uint32_t max_size = gpu.limits().max_texture_size_2d; max_size != 0)
        {
            shadow_map_size = std::min(shadow_map_size, max_size);
        }

        // Off-screen depth-only target: the sampled depth32_float
        // attachment the lit materials read is its only attachment.
        // begin_render_pass clears and z-tests against it automatically.
        m_target = gpu.create_render_target(gpu::render_target_descriptor::depth_only(
            gpu::texture_format::depth32_float, shadow_map_size, shadow_map_size));
        m_depth_texture = gpu.render_target_depth_texture(m_target);

        // Vertex stage only: with no colour attachment there is nothing
        // for a fragment stage to write, and the rasteriser writes the
        // depth the lit materials sample.
        gpu::shader_module_descriptor vs_descriptor{};
        vs_descriptor.stage = gpu::shader_stage::vertex;
        vs_descriptor.spirv = gpu::compile_library_shader("passes/shadow.vert.glsl", gpu::shader_stage::vertex);
        m_vertex_shader = gpu.create_shader_module(vs_descriptor);

        // Light-frame layout (slot 0): the view-projection UBO.
        gpu::bind_group_layout_descriptor light_layout{};
        light_layout.entries.push_back({light_frame_binding, gpu::binding_kind::uniform_buffer});
        m_light_layout = gpu.create_bind_group_layout(light_layout);

        // Per-draw layout (slot 1): the model matrix UBO at binding 1,
        // identical to the layout every 3D renderable builds its
        // per-draw bind group against, so those bind groups bind here
        // unchanged, at the same dynamic offset into the per-draw ring.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back(per_draw_model_layout_entry());
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
        // as the ground plane still occludes. The rasteriser's
        // slope-scaled depth bias pushes each caster's stored depth
        // away from the light in proportion to its slope, so grazing
        // faces do not self-shadow; the lit shader's receiver-side
        // bias (@ref depth_bias) stays on top of it for the PCF kernel.
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

        gpu::depth_bias_state depth_bias{};
        depth_bias.enabled = true;
        depth_bias.constant = shadow_depth_bias_constant;
        depth_bias.slope = std::max(settings.slope_bias, 0.0f);

        gpu::pipeline_descriptor pipeline_descriptor{};
        pipeline_descriptor.vertex_shader = m_vertex_shader;
        pipeline_descriptor.vertex_buffers.push_back(vertex_layout);
        pipeline_descriptor.depth = depth;
        pipeline_descriptor.blend = blend;
        pipeline_descriptor.rasterizer = rasterizer;
        pipeline_descriptor.depth_bias = depth_bias;
        pipeline_descriptor.bind_group_layouts.push_back(m_light_layout);
        pipeline_descriptor.bind_group_layouts.push_back(m_draw_layout);
        m_pipeline = gpu.create_pipeline(pipeline_descriptor);

        // Instanced casters rasterize with the same state through the
        // pipeline that reads their per-instance transform stream.
        m_instanced = create_instanced_shadow_pipeline(m_light_layout, depth, blend, rasterizer, depth_bias);
    }

    spot_shadow_pass::~spot_shadow_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;
        destroy_instanced_shadow_pipeline(m_instanced);
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
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
        // The depth texture is owned by the render target, so destroying
        // the target releases it.
        if (m_target.valid())
        {
            gpu.destroy(m_target);
            m_target = {};
            m_depth_texture = {};
        }
    }

    gpu::texture spot_shadow_pass::shadow_map() const
    {
        return m_depth_texture;
    }

    const core::math::mat4& spot_shadow_pass::light_view_projection() const
    {
        return m_light_view_projection;
    }

    bool spot_shadow_pass::has_shadow() const
    {
        return m_has_shadow;
    }

    int spot_shadow_pass::shadow_spot_index() const
    {
        return m_shadow_spot_index;
    }

    float spot_shadow_pass::depth_bias() const
    {
        return shadow_bias;
    }

    uint32_t spot_shadow_pass::culled_count() const
    {
        return m_culled;
    }

    void spot_shadow_pass::set_caster_mask(uint32_t mask) noexcept
    {
        m_caster_mask = mask;
    }

    uint32_t spot_shadow_pass::caster_mask() const noexcept
    {
        return m_caster_mask;
    }

    void spot_shadow_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        // Nothing in the frame context shapes a spot map: it is a fixed
        // perspective view from the light, independent of the camera, so
        // the pass reads only the light registry and the renderable
        // registry, like point_shadow_pass.
        auto& gpu = *runtime::current_engine().gpu;
        m_culled = 0;

        // Locate the first shadow-casting spot light, tracking its index
        // within the packed spot array so the lit shader can match it.
        const spot_light* caster = nullptr;
        m_shadow_spot_index = -1;
        int spot_index = 0;
        for (const light* l : registered_lights())
        {
            if (l->type() != light_type::spot)
            {
                continue;
            }
            if (spot_index >= static_cast<int>(max_spot_lights))
            {
                break;
            }
            const auto* sl = static_cast<const spot_light*>(l);
            if (sl->cast_shadow)
            {
                caster = sl;
                m_shadow_spot_index = spot_index;
                break;
            }
            ++spot_index;
        }

        m_has_shadow = caster != nullptr;

        // Always open the pass so the depth map is cleared even on
        // no-caster frames; the lit shader keys off has_shadow rather
        // than the (possibly stale) contents. The target has no colour
        // attachment, so only the depth ops matter.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = m_target;
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::clear;
        descriptor.depth.clear_depth = 1.0f;

        auto pass_encoder = encoder.begin_render_pass(descriptor);

        if (!m_has_shadow)
        {
            pass_encoder->end();
            return;
        }

        // Build the light's perspective view-projection: the vertical FOV
        // is twice the outer cone half-angle (clamped away from the
        // degenerate edges), the far plane follows the caster's range,
        // and the basis is built on the engine up axis; reference_up
        // swaps in a horizontal axis for a light pointing straight up or
        // down so look_at stays well-defined.
        const math::vec3 dir = math::normalize(caster->direction);
        const math::vec3 up = math::reference_up(dir);
        const float outer = std::clamp(caster->outer_angle, min_outer_angle, max_outer_angle);
        const math::mat4 view = math::look_at(caster->position, caster->position + dir, up);
        const math::mat4 projection = math::perspective(outer * 2.0f, 1.0f, light_near, far_plane(*caster));
        m_light_view_projection = projection * view;

        gpu.write_buffer(m_light_ubo, m_light_view_projection.data(), sizeof(math::mat4), 0);

        // Every scene renderable casts. Reuse the per-draw model-matrix
        // bind group each renderable already built (or, for an instanced
        // batch, its per-instance transform stream); the depth-only
        // pipelines read only position so the differing vertex strides
        // are absorbed by the per-draw stride override. Casters whose
        // bounds lie outside the light's perspective frustum could never
        // rasterize into the map, so they are skipped before their items
        // are even built; a renderable without bounds always casts.
        const math::frustum light_frustum = math::frustum::from_view_projection(m_light_view_projection);
        m_items.clear();
        for (auto* r : *m_registry)
        {
            if (!r->casts_shadow() || (r->layer_mask & m_caster_mask) == 0)
            {
                continue;
            }
            math::aabb bounds;
            if (r->world_bounds(bounds) && !light_frustum.intersects(bounds))
            {
                ++m_culled;
                continue;
            }
            r->collect_draw_items(m_items);
        }

        shadow_caster_dispatch dispatch(*pass_encoder, m_pipeline, m_instanced.pipeline, m_light_bind_group);
        for (const auto& item : m_items)
        {
            dispatch.draw(item);
        }

        pass_encoder->end();
    }
} // namespace rendering_engine
