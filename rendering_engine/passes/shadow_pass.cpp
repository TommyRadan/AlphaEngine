// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/passes/shadow_pass.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/gpu/texture.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>

namespace
{
    namespace math = core::math;

    using rendering_engine::max_shadow_cascades;

    // Fallback used only when no camera is attached, so the first cascade
    // still holds something sensible to clear / sample: a sphere of this
    // radius centred on the world origin, boxed along the light direction
    // like any cascade.
    constexpr float fallback_radius = 6.0f;

    // Split scheme: each split depth blends the logarithmic split (even
    // texel density under perspective, which crowds the near cascades)
    // with the uniform one (equal depth slices) at this weight. Leaning
    // logarithmic keeps the near cascades tight while the far ones still
    // cover a useful slice.
    constexpr float split_lambda = 0.75f;

    // The log term needs a strictly positive start; a near plane at or
    // behind the eye (an orthographic camera) starts it here instead.
    constexpr float min_log_split_near = 0.01f;

    // Width of the cross-fade band before each split, as a fraction of
    // the cascade's depth range. The next cascade's slice is widened to
    // start at the band so both cascades hold the receivers in it.
    constexpr float cascade_blend_fraction = 0.1f;

    // How far each cascade's box reaches back toward the light at
    // least, as a multiple of its radius: casters between the light and
    // the visible region rasterize into the map even when they report no
    // bounds, and bounded casters push the box further back as needed.
    // A box this deep is also the reference the receiver bias is
    // expressed against (see shadow_pass::depth_bias).
    constexpr float caster_depth_scale = 6.0f;

    // Constant term of the depth-only pipeline's rasteriser depth bias:
    // one resolvable depth step. On the float depth format that step is
    // tiny, so the slope term (rendering_engine::shadow_settings::slope_bias times
    // the caster's depth slope) does the work of lifting grazing casters
    // clear of their own samples, and the lit shader's receiver-side
    // bias covers the rest of the PCF footprint.
    constexpr float shadow_depth_bias_constant = 1.0f;

    // Binding number of the per-cascade view-projection UBO within the
    // depth-only pipeline's light set (slot 0).
    constexpr uint32_t light_frame_binding = 0;

    // One of the camera frustum's four side edges: its near- and
    // far-plane corners in world space and the view depth of each.
    struct frustum_edge
    {
        math::vec3 near_point{0.0f, 0.0f, 0.0f};
        math::vec3 far_point{0.0f, 0.0f, 0.0f};
        float near_depth{0.0f};
        float far_depth{0.0f};
    };

    // The receivers one cascade covers: a world-space sphere around its
    // slice of the view frustum.
    struct cascade_sphere
    {
        math::vec3 center{0.0f, 0.0f, 0.0f};
        float radius{1.0f};
    };

    // A cascade's orthographic box in light space (the light's rotation,
    // eye at the origin, looking down -z along the light direction):
    // the texel-snapped sphere centre, the half extent, and how far the
    // box reaches from the centre back toward the light.
    struct cascade_box
    {
        math::vec3 center{0.0f, 0.0f, 0.0f};
        float radius{1.0f};
        float reach{1.0f};
    };

    // Unprojects the eight corners of the clip volume (x and y in
    // [-1, 1], depth 0 on the near plane and 1 on the far plane) through
    // the camera and pairs them into the frustum's four side edges. The
    // camera looks down -z in view space, so forward depth is
    // -(view * p).z.
    std::array<frustum_edge, 4> camera_frustum_edges(const math::mat4& view, const math::mat4& projection)
    {
        const math::mat4 inverse_view_proj = math::inverse(projection * view);
        std::array<frustum_edge, 4> edges{};
        std::size_t count = 0;
        for (int xi = 0; xi < 2; ++xi)
        {
            for (int yi = 0; yi < 2; ++yi)
            {
                const float x = xi == 0 ? -1.0f : 1.0f;
                const float y = yi == 0 ? -1.0f : 1.0f;

                const math::vec4 near_h = inverse_view_proj * math::vec4{x, y, 0.0f, 1.0f};
                const math::vec4 far_h = inverse_view_proj * math::vec4{x, y, 1.0f, 1.0f};

                frustum_edge& edge = edges[count++];
                edge.near_point = math::vec3{near_h.x / near_h.w, near_h.y / near_h.w, near_h.z / near_h.w};
                edge.far_point = math::vec3{far_h.x / far_h.w, far_h.y / far_h.w, far_h.z / far_h.w};
                edge.near_depth = -(view * math::vec4{edge.near_point, 1.0f}).z;
                edge.far_depth = -(view * math::vec4{edge.far_point, 1.0f}).z;
            }
        }
        return edges;
    }

    // The point of @p edge at view depth @p depth. View-space depth is
    // linear along each near -> far edge, so this is a plain world-space
    // lerp, clamped to the edge.
    math::vec3 point_at_depth(const frustum_edge& edge, float depth)
    {
        float t = 1.0f;
        if (edge.far_depth > edge.near_depth)
        {
            t = std::clamp((depth - edge.near_depth) / (edge.far_depth - edge.near_depth), 0.0f, 1.0f);
        }
        return math::lerp(edge.near_point, edge.far_point, t);
    }

    // Bounding sphere of the frustum slice between view depths @p begin
    // and @p end. It depends only on the slice's shape, never on the
    // camera's orientation, so the cascade keeps a constant size as the
    // camera turns; the radius is quantized so sub-texel size jitter from
    // the unprojection does not shimmer.
    cascade_sphere fit_slice(const std::array<frustum_edge, 4>& edges, float begin, float end)
    {
        std::array<math::vec3, 8> corners{};
        for (std::size_t i = 0; i < edges.size(); ++i)
        {
            corners[2 * i] = point_at_depth(edges[i], begin);
            corners[2 * i + 1] = point_at_depth(edges[i], end);
        }

        cascade_sphere sphere{};
        for (const auto& c : corners)
        {
            sphere.center += c;
        }
        sphere.center /= static_cast<float>(corners.size());

        float radius = 0.0f;
        for (const auto& c : corners)
        {
            radius = std::max(radius, math::length(c - sphere.center));
        }
        sphere.radius = std::max(std::ceil(radius * 16.0f) / 16.0f, 1.0f / 16.0f);
        return sphere;
    }

    // Splits the camera's view depth, from its near plane out to
    // @p distance (or its far plane, if closer), into @p count slices by
    // the log / uniform blend, and fits a sphere to each. @p splits
    // receives each slice's far depth. A slice after the first starts at
    // the previous one's blend band (the same band the lit shader
    // cross-fades over: cascade_blend_fraction of the previous slice's
    // range, measured from the split before it or from 0), so the
    // receivers there sit inside both cascades.
    void fit_cascades(const math::mat4& view,
                      const math::mat4& projection,
                      int count,
                      float distance,
                      std::array<float, max_shadow_cascades>& splits,
                      std::array<cascade_sphere, max_shadow_cascades>& spheres)
    {
        const std::array<frustum_edge, 4> edges = camera_frustum_edges(view, projection);
        float near_depth = edges[0].near_depth;
        float far_depth = edges[0].far_depth;
        for (const auto& edge : edges)
        {
            near_depth = std::min(near_depth, edge.near_depth);
            far_depth = std::max(far_depth, edge.far_depth);
        }
        far_depth = std::max(std::min(far_depth, distance), near_depth + min_log_split_near);

        const float log_near = std::max(near_depth, min_log_split_near);
        for (int i = 0; i < count; ++i)
        {
            const float fraction = static_cast<float>(i + 1) / static_cast<float>(count);
            const float log_split = log_near * std::pow(far_depth / log_near, fraction);
            const float uniform_split = near_depth + (far_depth - near_depth) * fraction;
            splits[i] = split_lambda * log_split + (1.0f - split_lambda) * uniform_split;
        }
        splits[count - 1] = far_depth;

        for (int i = 0; i < count; ++i)
        {
            float begin = near_depth;
            if (i > 0)
            {
                const float previous_begin = i > 1 ? splits[i - 2] : 0.0f;
                begin = splits[i - 1] - cascade_blend_fraction * (splits[i - 1] - previous_begin);
            }
            spheres[i] = fit_slice(edges, std::max(begin, near_depth), splits[i]);
        }
    }

    // The light-space box of @p sphere, its centre snapped to whole
    // texels of a @p resolution map so the shadow edges do not crawl as
    // the camera moves, reaching back toward the light by the default
    // caster depth.
    cascade_box light_space_box(const cascade_sphere& sphere, const math::mat4& light_rotation, uint32_t resolution)
    {
        cascade_box box{};
        box.radius = sphere.radius;
        box.reach = sphere.radius * caster_depth_scale;

        const math::vec4 center = light_rotation * math::vec4{sphere.center, 1.0f};
        const float texel = (2.0f * sphere.radius) / static_cast<float>(resolution);
        box.center = math::vec3{std::floor(center.x / texel) * texel, std::floor(center.y / texel) * texel, center.z};
        return box;
    }

    // Whether a caster with light-space bounds @p bounds can shadow a
    // receiver in @p box: it must overlap the box across the light
    // direction and must not lie wholly past the receivers' far side
    // (further from the light than the whole sphere).
    bool caster_reaches(const cascade_box& box, const math::aabb& bounds)
    {
        return bounds.max.x >= box.center.x - box.radius && bounds.min.x <= box.center.x + box.radius &&
               bounds.max.y >= box.center.y - box.radius && bounds.min.y <= box.center.y + box.radius &&
               bounds.max.z >= box.center.z - box.radius;
    }

    // The cascade's view-projection: an orthographic box over the light
    // rotation. View space looks down -z, so the near plane (depth 0)
    // sits @c reach toward the light from the centre and the far plane
    // (depth 1) on the receivers' far side.
    math::mat4 box_view_projection(const cascade_box& box, const math::mat4& light_rotation)
    {
        const math::mat4 projection = math::ortho(box.center.x - box.radius,
                                                  box.center.x + box.radius,
                                                  box.center.y - box.radius,
                                                  box.center.y + box.radius,
                                                  -(box.center.z + box.reach),
                                                  -(box.center.z - box.radius));
        return projection * light_rotation;
    }
} // namespace

namespace rendering_engine
{
    shadow_pass::shadow_pass(gpu::device& device, const rendering_engine::shadow_settings& settings)
        : m_device(&device), m_resolution(std::max(settings.resolution, 1u)),
          m_cascade_count(std::clamp(static_cast<int>(settings.cascade_count), 1, max_shadow_cascades)),
          m_distance(std::max(settings.distance, min_log_split_near)), m_bias(std::max(settings.bias, 0.0f)),
          m_pcf_kernel(std::clamp(settings.pcf_kernel, 1u, rendering_engine::shadow_settings::max_pcf_kernel))
    {
        auto& gpu = *m_device;

        // Every cascade is a full square layer, so the configured size
        // must fit the device's 2D limit.
        const uint32_t max_size = gpu.limits().max_texture_size_2d;
        if (max_size != 0)
        {
            m_resolution = std::min(m_resolution, max_size);
        }

        // One depth array, sampled by the lit materials as a
        // sampler2DArrayShadow, and one depth-only target per cascade
        // attached to its layer. The texture's own sampler state is
        // never used: the scene pass binds the comparison sampler below
        // at the same binding.
        gpu::texture_descriptor array_descriptor{};
        array_descriptor.dimension = gpu::texture_dimension::d2_array;
        array_descriptor.format = gpu::texture_format::depth32_float;
        array_descriptor.width = m_resolution;
        array_descriptor.height = m_resolution;
        array_descriptor.array_layers = static_cast<uint32_t>(m_cascade_count);
        array_descriptor.usage = gpu::texture_usage_default | gpu::texture_usage_render_attachment;
        array_descriptor.min_filter = gpu::filter_mode::nearest;
        array_descriptor.mag_filter = gpu::filter_mode::nearest;
        array_descriptor.mipmap_filter = gpu::mipmap_mode::none;
        array_descriptor.address_u = gpu::address_mode::clamp_edge;
        array_descriptor.address_v = gpu::address_mode::clamp_edge;
        array_descriptor.address_w = gpu::address_mode::clamp_edge;
        m_depth_texture = gpu.create_texture(array_descriptor);

        for (int cascade = 0; cascade < m_cascade_count; ++cascade)
        {
            gpu::render_target_descriptor target_descriptor{};
            target_descriptor.width = m_resolution;
            target_descriptor.height = m_resolution;
            target_descriptor.with_depth = true;
            target_descriptor.depth.texture = m_depth_texture;
            target_descriptor.depth.layer = static_cast<uint32_t>(cascade);
            m_targets[cascade] = gpu.create_render_target(target_descriptor);
        }

        // Hardware PCF: each lookup compares the reference depth against
        // the four nearest texels and returns their bilinearly weighted
        // pass fraction. A receiver passes (is lit) when its biased depth
        // is at or before the stored occluder depth.
        gpu::sampler_descriptor compare_descriptor{};
        compare_descriptor.min_filter = gpu::filter_mode::linear;
        compare_descriptor.mag_filter = gpu::filter_mode::linear;
        compare_descriptor.mipmap = gpu::mipmap_mode::none;
        compare_descriptor.address_u = gpu::address_mode::clamp_edge;
        compare_descriptor.address_v = gpu::address_mode::clamp_edge;
        compare_descriptor.address_w = gpu::address_mode::clamp_edge;
        compare_descriptor.compare_enabled = true;
        compare_descriptor.compare = gpu::compare_function::less_equal;
        m_compare_sampler = gpu.create_sampler(compare_descriptor);

        // Vertex stage only: with no colour attachment there is nothing
        // for a fragment stage to write, and the rasteriser writes the
        // depth the lit materials sample.
        m_vertex_shader = gpu::create_library_shader_module(gpu, "passes/shadow.vert.glsl", gpu::shader_stage::vertex);

        // Light-frame layout (slot 0): the view-projection UBO.
        gpu::bind_group_layout_descriptor light_layout{};
        light_layout.entries.push_back({light_frame_binding, gpu::binding_kind::uniform_buffer});
        m_light_layout = gpu.create_bind_group_layout(light_layout);

        // One view-projection UBO and bind group per cascade, so each
        // cascade's pass binds its own matrix without rewriting a buffer
        // an earlier pass in the same frame still reads.
        for (int cascade = 0; cascade < m_cascade_count; ++cascade)
        {
            gpu::buffer_descriptor ubo_descriptor{};
            ubo_descriptor.size = sizeof(math::mat4);
            ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
            ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
            m_light_ubos[cascade] = gpu.create_buffer(ubo_descriptor);

            gpu::bind_group_descriptor light_bind_group_descriptor{};
            light_bind_group_descriptor.layout = m_light_layout;
            gpu::binding_value light_slot{};
            light_slot.binding = light_frame_binding;
            light_slot.kind = gpu::binding_kind::uniform_buffer;
            light_slot.buffer_value = m_light_ubos[cascade];
            light_bind_group_descriptor.entries.push_back(light_slot);
            m_light_bind_groups[cascade] = gpu.create_bind_group(light_bind_group_descriptor);
        }

        // Depth-only opaque draw: position-only vertex stream (offset 0
        // of every mesh's vertex record), depth tested and
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
        // The casters push their PerDraw block; there is no per-draw set.
        pipeline_descriptor.push_constant_ranges.push_back(per_draw_push_constant_range());
        m_pipeline = gpu.create_pipeline(pipeline_descriptor);

        // Instanced casters rasterize with the same state through the
        // pipeline that reads their per-instance transform stream.
        m_instanced = create_instanced_shadow_pipeline(*m_device, m_light_layout, depth, blend, rasterizer, depth_bias);
    }

    shadow_pass::~shadow_pass()
    {
        auto& gpu = *m_device;
        destroy_instanced_shadow_pipeline(*m_device, m_instanced);
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
        if (m_compare_sampler.valid())
        {
            gpu.destroy(m_compare_sampler);
            m_compare_sampler = {};
        }
        // The layer targets import the array, so they go first and the
        // array last.
        for (auto& target : m_targets)
        {
            if (target.valid())
            {
                gpu.destroy(target);
                target = {};
            }
        }
        if (m_depth_texture.valid())
        {
            gpu.destroy(m_depth_texture);
            m_depth_texture = {};
        }
    }

    gpu::texture shadow_pass::shadow_map() const
    {
        return m_depth_texture;
    }

    gpu::sampler shadow_pass::shadow_sampler() const
    {
        return m_compare_sampler;
    }

    int shadow_pass::cascade_count() const
    {
        return m_active_cascades;
    }

    const core::math::mat4& shadow_pass::light_view_projection(int cascade) const
    {
        return m_light_view_projections[cascade];
    }

    float shadow_pass::split_depth(int cascade) const
    {
        return m_split_depths[cascade];
    }

    float shadow_pass::depth_bias(int cascade) const
    {
        return m_depth_biases[cascade];
    }

    float shadow_pass::cascade_blend() const
    {
        return cascade_blend_fraction;
    }

    uint32_t shadow_pass::pcf_kernel() const
    {
        return m_pcf_kernel;
    }

    bool shadow_pass::has_shadow() const
    {
        return m_has_shadow;
    }

    int shadow_pass::shadow_light_index() const
    {
        return m_shadow_light_index;
    }

    uint32_t shadow_pass::culled_count() const
    {
        return m_culled;
    }

    void shadow_pass::set_caster_mask(uint32_t mask) noexcept
    {
        m_caster_mask = mask;
    }

    uint32_t shadow_pass::caster_mask() const noexcept
    {
        return m_caster_mask;
    }

    void shadow_pass::prepare(const frame_context& ctx)
    {
        auto& gpu = *m_device;
        m_culled = 0;

        // Locate the first shadow-casting directional light, tracking
        // its index within the packed directional array so the lit
        // shader can match it. Lights past the UBO capacity never reach
        // the shader, so they cannot cast.
        const light_proxy* caster = nullptr;
        m_shadow_light_index = -1;
        int directional_index = 0;
        for (const light_proxy* l : ctx.lights)
        {
            if (l->type != light_type::directional)
            {
                continue;
            }
            if (directional_index >= static_cast<int>(max_directional_lights))
            {
                break;
            }
            if (l->cast_shadow)
            {
                caster = l;
                m_shadow_light_index = directional_index;
                break;
            }
            ++directional_index;
        }

        m_has_shadow = caster != nullptr;
        m_active_cascades = 0;
        m_items.clear();
        m_casters.clear();

        if (m_has_shadow)
        {
            // Every cascade shares the light's rotation, built on the
            // engine up axis (+Z); reference_up swaps in a horizontal axis
            // for a light pointing straight up or down so look_at stays
            // well-defined. Casters are boxed in this space once and
            // tested against every cascade's box there.
            const math::vec3 dir = math::normalize(caster->direction);
            const math::vec3 up = math::reference_up(dir);
            const math::mat4 light_rotation = math::look_at(math::vec3{0.0f, 0.0f, 0.0f}, dir, up);

            // With a camera, split its view depth into the cascades and
            // fit each to its slice so the texels land on what the viewer
            // sees, densest up close; otherwise fall back to one fixed box
            // centred on the origin. The camera is the frame's, so the fit
            // matches what the scene pass renders even if the arbitration
            // changes mid-frame.
            std::array<cascade_sphere, max_shadow_cascades> spheres{};
            if (const camera_proxy* cam = ctx.active_camera; cam != nullptr)
            {
                m_active_cascades = m_cascade_count;
                fit_cascades(cam->view, cam->projection, m_active_cascades, m_distance, m_split_depths, spheres);
            }
            else
            {
                m_active_cascades = 1;
                spheres[0].radius = fallback_radius;
                m_split_depths[0] = m_distance;
            }

            std::array<cascade_box, max_shadow_cascades> boxes{};
            for (int cascade = 0; cascade < m_active_cascades; ++cascade)
            {
                boxes[cascade] = light_space_box(spheres[cascade], light_rotation, m_resolution);
            }

            // Walk the draws once per frame, not once per cascade.
            // A caster with bounds is tested against each
            // cascade's box in light space: one that cannot reach a
            // cascade is culled there, one that cannot reach any never
            // builds its draw items, and every one that does reach a
            // cascade pushes that cascade's box back toward the light far
            // enough to hold it, so a tall occluder outside the view
            // still casts into the near cascades. A caster without bounds
            // casts into every cascade.
            const uint32_t all_cascades = (1u << static_cast<uint32_t>(m_active_cascades)) - 1u;
            for (const mesh_draw& draw : ctx.scene_draws)
            {
                if (!draw.casts_shadow || (draw.layer_mask & m_caster_mask) == 0)
                {
                    continue;
                }
                uint32_t reached = all_cascades;
                if (draw.bounded)
                {
                    const math::aabb light_bounds = math::transform(draw.bounds, light_rotation);
                    reached = 0;
                    for (int cascade = 0; cascade < m_active_cascades; ++cascade)
                    {
                        cascade_box& box = boxes[cascade];
                        if (!caster_reaches(box, light_bounds))
                        {
                            ++m_culled;
                            continue;
                        }
                        reached |= 1u << static_cast<uint32_t>(cascade);
                        box.reach = std::max(box.reach, light_bounds.max.z - box.center.z);
                    }
                    if (reached == 0)
                    {
                        continue;
                    }
                }

                caster_range range{};
                range.first = m_items.size();
                range.cascades = reached;
                if (draw.drawable)
                {
                    m_items.push_back(draw.item);
                }
                range.count = m_items.size() - range.first;
                if (range.count != 0)
                {
                    m_casters.push_back(range);
                }
            }

            // The boxes are final: build each cascade's matrix, and scale
            // the receiver bias so it stays the same share of the
            // cascade's radius however far the box now reaches back. Each
            // active cascade's matrix goes to its own UBO now, so record()
            // only binds.
            for (int cascade = 0; cascade < m_active_cascades; ++cascade)
            {
                const cascade_box& box = boxes[cascade];
                m_light_view_projections[cascade] = box_view_projection(box, light_rotation);
                m_depth_biases[cascade] = m_bias * (caster_depth_scale + 1.0f) * box.radius / (box.reach + box.radius);
                gpu.write_buffer(
                    m_light_ubos[cascade], m_light_view_projections[cascade].data(), sizeof(math::mat4), 0);
            }
        }
    }

    void shadow_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        // Render each cascade's layer. Every layer is cleared, even with
        // no caster, so the lit shader keys off the enabled flag and the
        // cascade count rather than stale depth; the target has no colour
        // attachment, so only the depth ops matter.
        for (int cascade = 0; cascade < m_cascade_count; ++cascade)
        {
            const bool active = cascade < m_active_cascades;

            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_targets[cascade];
            descriptor.use_depth = true;
            descriptor.depth.load = gpu::load_op::clear;
            descriptor.depth.clear_depth = 1.0f;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            if (!active)
            {
                pass_encoder->end();
                continue;
            }

            // Only the casters that reach this cascade. Reuse the
            // per-draw block each mesh proxy carries (or, for an
            // instanced batch, its per-instance transform stream); the
            // depth-only pipelines read only position so the differing
            // vertex strides are absorbed by the per-draw stride override.
            const uint32_t bit = 1u << static_cast<uint32_t>(cascade);
            shadow_caster_dispatch dispatch(
                *pass_encoder, m_pipeline, m_instanced.pipeline, m_light_bind_groups[cascade]);
            for (const auto& range : m_casters)
            {
                if ((range.cascades & bit) == 0)
                {
                    continue;
                }
                for (std::size_t i = range.first; i < range.first + range.count; ++i)
                {
                    dispatch.draw(m_items[i]);
                }
            }

            pass_encoder->end();
        }
    }
} // namespace rendering_engine
