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

#include <algorithm>
#include <string>

#include <rendering_engine/gpu/bind_group.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/pipeline.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/gpu/shader.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/lighting/point_light.hpp>
#include <rendering_engine/renderables/per_draw_ubo.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <runtime/engine.hpp>

namespace
{
    namespace math = core::math;

    // Perspective frustum per face: 90 degrees covers exactly one cube face.
    // The near plane hugs the light; the far plane is the caster's
    // point_light::range — nothing beyond it receives the light, so nothing
    // beyond it needs a shadow — which keeps the perspective depth precise
    // enough to avoid acne. A light with no cutoff (range 0) falls back to
    // the fixed default.
    constexpr float light_near = 0.1f;
    constexpr float default_light_far = 20.0f;
    constexpr float shadow_bias = 0.0025f;

    // The face far plane for a caster: its range, or the default when it
    // has no cutoff. Never closer than the near plane.
    float face_far_plane(const rendering_engine::point_light& caster)
    {
        const float far_plane = caster.range > 0.0f ? caster.range : default_light_far;
        return far_plane > light_near ? far_plane : default_light_far;
    }

    // Constant term of the depth-only pipeline's rasteriser depth bias
    // (see shadow_pass for the rationale): one depth step. The slope term
    // is core::shadow_settings::slope_bias times the caster's slope.
    constexpr float shadow_depth_bias_constant = 1.0f;

    constexpr uint32_t light_frame_binding = 0;

    // Look direction and up for each of the six faces, in cube-map face
    // order +X, -X, +Y, -Y, +Z, -Z. The ups are the ones the cube-map face
    // convention prescribes: a face rendered through look_at(light, dir,
    // up) then lands with row 0 / column 0 where a samplerCube lookup
    // expects them (for the +X face, +Y at the first row and +Z at the
    // first column), so the hardware face selection and the rendered
    // image agree. Both backends write off-screen row 0 for NDC y = -1,
    // so one basis serves both.
    struct face_basis
    {
        math::vec3 dir;
        math::vec3 up;
    };

    constexpr std::array<face_basis, 6> face_bases = {{
        {{1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
        {{-1.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}},
        {{0.0f, 0.0f, -1.0f}, {0.0f, -1.0f, 0.0f}},
    }};
} // namespace

namespace rendering_engine
{
    point_shadow_pass::point_shadow_pass(std::vector<renderable*>* registry, const core::shadow_settings& settings)
        : m_registry(registry)
    {
        auto& gpu = *runtime::current_engine().gpu;

        // Per-face resolution: half the configured shadow resolution,
        // because six faces are kept resident (the default 2048 gives
        // 1024-texel faces, plenty for the demo's small bodies). Clamped
        // to the device's cube limit.
        uint32_t shadow_map_size = std::max(settings.resolution / 2u, 1u);
        if (const uint32_t max_size = gpu.limits().max_texture_size_cube; max_size != 0)
        {
            shadow_map_size = std::min(shadow_map_size, max_size);
        }

        // One depth cube map, sampled by the lit materials as a
        // samplerCube, and six depth-only targets each attached to one of
        // its faces. Nearest, clamped sampling: the lit shader does its
        // own PCF over raw depth.
        gpu::texture_descriptor cube_descriptor{};
        cube_descriptor.dimension = gpu::texture_dimension::cube;
        cube_descriptor.format = gpu::texture_format::depth32_float;
        cube_descriptor.width = shadow_map_size;
        cube_descriptor.height = shadow_map_size;
        cube_descriptor.usage = gpu::texture_usage_default | gpu::texture_usage_render_attachment;
        cube_descriptor.min_filter = gpu::filter_mode::nearest;
        cube_descriptor.mag_filter = gpu::filter_mode::nearest;
        cube_descriptor.mipmap_filter = gpu::mipmap_mode::none;
        cube_descriptor.address_u = gpu::address_mode::clamp_edge;
        cube_descriptor.address_v = gpu::address_mode::clamp_edge;
        cube_descriptor.address_w = gpu::address_mode::clamp_edge;
        m_depth_texture = gpu.create_texture(cube_descriptor);

        for (int face = 0; face < point_shadow_face_count; ++face)
        {
            gpu::render_target_descriptor target_descriptor{};
            target_descriptor.width = shadow_map_size;
            target_descriptor.height = shadow_map_size;
            target_descriptor.with_depth = true;
            target_descriptor.depth.texture = m_depth_texture;
            target_descriptor.depth.layer = static_cast<uint32_t>(face);
            m_targets[face] = gpu.create_render_target(target_descriptor);
        }

        // Vertex stage only: the faces have no colour attachment, and
        // the rasteriser writes the depth the lit materials sample.
        m_vertex_shader = gpu::create_library_shader_module(gpu, "passes/shadow.vert.glsl", gpu::shader_stage::vertex);

        gpu::bind_group_layout_descriptor light_layout{};
        light_layout.entries.push_back({light_frame_binding, gpu::binding_kind::uniform_buffer});
        m_light_layout = gpu.create_bind_group_layout(light_layout);

        // The renderables' per-draw groups bind here unchanged, at the
        // same dynamic offset into the per-draw ring.
        gpu::bind_group_layout_descriptor draw_layout{};
        draw_layout.entries.push_back(per_draw_model_layout_entry());
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

        // Instanced casters rasterize with the same state (back-face culling
        // included) through the pipeline that reads their transform stream.
        m_instanced = create_instanced_shadow_pipeline(m_light_layout, depth, blend, rasterizer, depth_bias);
    }

    point_shadow_pass::~point_shadow_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;
        destroy_instanced_shadow_pipeline(m_instanced);
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
        if (m_vertex_shader.valid())
        {
            gpu.destroy(m_vertex_shader);
            m_vertex_shader = {};
        }
        // The face targets import the cube, so they go first and the
        // cube last.
        for (int face = 0; face < point_shadow_face_count; ++face)
        {
            if (m_targets[face].valid())
            {
                gpu.destroy(m_targets[face]);
                m_targets[face] = {};
            }
        }
        if (m_depth_texture.valid())
        {
            gpu.destroy(m_depth_texture);
            m_depth_texture = {};
        }
    }

    gpu::texture point_shadow_pass::shadow_map() const
    {
        return m_depth_texture;
    }

    const core::math::mat4& point_shadow_pass::light_view_projection(int face) const
    {
        return m_light_view_projections[face];
    }

    const core::math::vec3& point_shadow_pass::light_position() const
    {
        return m_light_position;
    }

    float point_shadow_pass::shadow_near() const
    {
        return light_near;
    }

    float point_shadow_pass::shadow_far() const
    {
        return m_light_far;
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

    uint32_t point_shadow_pass::culled_count() const
    {
        return m_culled;
    }

    void point_shadow_pass::set_caster_mask(uint32_t mask) noexcept
    {
        m_caster_mask = mask;
    }

    uint32_t point_shadow_pass::caster_mask() const noexcept
    {
        return m_caster_mask;
    }

    void point_shadow_pass::record(gpu::command_encoder& encoder, const frame_context& /*ctx*/)
    {
        // Nothing in the frame context shapes an omni map: the six faces
        // are fixed 90-degree views from the light, independent of the
        // camera, so the pass reads only the light registry and the
        // renderable registry.
        auto& gpu = *runtime::current_engine().gpu;
        m_culled = 0;

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

        // 90-degree vertical FOV (pi/2), square aspect: exactly one cube face.
        // The far plane follows the caster's range; the matrix is only used
        // (and the position only read) when there is a caster.
        constexpr float face_fov_y = 1.57079633f;
        math::mat4 projection{};
        if (m_has_shadow)
        {
            m_light_position = caster->position;
            m_light_far = face_far_plane(*caster);
            projection = math::perspective(face_fov_y, 1.0f, light_near, m_light_far);
        }

        // Walk the registry once per frame, not once per face: every caster
        // builds its draw items (and writes its per-draw UBO) exactly once,
        // and its world bounds are recorded beside its item range so each
        // face below can cull against its own frustum without asking the
        // renderable again. A caster that reports no bounds casts into
        // every face.
        m_items.clear();
        m_casters.clear();
        if (m_has_shadow)
        {
            for (auto* r : *m_registry)
            {
                if (!r->casts_shadow() || (r->layer_mask & m_caster_mask) == 0)
                {
                    continue;
                }
                caster_range range{};
                range.first = m_items.size();
                range.bounded = r->world_bounds(range.bounds);
                r->collect_draw_items(m_items);
                range.count = m_items.size() - range.first;
                if (range.count != 0)
                {
                    m_casters.push_back(range);
                }
            }
        }

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

            // Depth-only face target: only the depth ops matter.
            gpu::render_pass_descriptor descriptor{};
            descriptor.target = m_targets[face];
            descriptor.use_depth = true;
            descriptor.depth.load = gpu::load_op::clear;
            descriptor.depth.clear_depth = 1.0f;

            auto pass_encoder = encoder.begin_render_pass(descriptor);
            if (!m_has_shadow)
            {
                pass_encoder->end();
                continue;
            }

            // Only casters whose bounds touch this face's 90-degree frustum
            // can rasterize into its map; the rest are skipped here without
            // touching their items. The dispatch picks the single-draw or
            // instanced pipeline per item and binds this face's light group
            // with it.
            shadow_caster_dispatch dispatch(*pass_encoder, m_pipeline, m_instanced.pipeline, m_light_bind_groups[face]);
            const math::frustum face_frustum = math::frustum::from_view_projection(m_light_view_projections[face]);
            for (const auto& caster : m_casters)
            {
                if (caster.bounded && !face_frustum.intersects(caster.bounds))
                {
                    ++m_culled;
                    continue;
                }

                for (std::size_t i = caster.first; i < caster.first + caster.count; ++i)
                {
                    dispatch.draw(m_items[i]);
                }
            }

            pass_encoder->end();
        }
    }
} // namespace rendering_engine
