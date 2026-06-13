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

#include <rendering_engine/passes/scene_pass.hpp>

#include <algorithm>
#include <array>
#include <cstring>

#include <core/event.hpp>
#include <core/event_engine.hpp>
#include <core/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/passes/point_shadow_pass.hpp>
#include <rendering_engine/passes/shadow_pass.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <runtime/engine.hpp>

namespace rendering_engine
{
    namespace
    {
        // std140 layout for the per-view PerFrame UBO: mat4 viewMatrix
        // at offset 0, mat4 projectionMatrix at offset 64, then the fog
        // block — vec4 fogColor at 128 (rgb colour, a = fog mode) and
        // vec4 fogParams at 144 (x near, y far, z density). mat4 / vec4
        // are 16-byte aligned, so no padding is needed between members.
        // 160 bytes total.
        constexpr size_t per_frame_ubo_size = 2 * sizeof(core::math::mat4) + 2 * sizeof(core::math::vec4);

        // Binding numbers within the per-frame bind group (slot 0). The
        // numbers must stay unique across both descriptor sets in a lit
        // pipeline because OpenGL flattens UBO bindings into a single
        // namespace through ARB_gl_spirv: binding 0 = the PerFrame view
        // block (camera matrices + fog), binding 1 = the per-draw model
        // matrix (set 1), so the lights block takes binding 2.
        constexpr uint32_t camera_binding = 0;
        constexpr uint32_t lights_binding = 2;

        // Directional shadow data shares the per-frame group. The lit
        // pipelines already spend UBO bindings 0..3 (camera, model,
        // lights, material params) and sampler bindings 4..8; following
        // the materials' convention of never reusing a number across the
        // two namespaces, the shadow map takes the next free sampler
        // binding 9 and the shadow UBO sits at 10.
        constexpr uint32_t shadow_map_binding = 9;
        constexpr uint32_t shadow_binding = 10;

        // std140 layout of the per-frame Shadow block: mat4
        // lightViewProj at offset 0, vec4 params at offset 64
        // (x enabled, y bias, z caster index). 80 bytes total.
        constexpr size_t shadow_ubo_size = sizeof(core::math::mat4) + 4 * sizeof(float);

        // Omni (point-light) shadow data also shares the per-frame group. The
        // next free binding number across both the UBO and sampler namespaces
        // (the directional set ends at 10, IBL spends 11..13) is 14 for the UBO
        // and 15..20 for the six face depth maps.
        constexpr uint32_t point_shadow_binding = 14;
        constexpr uint32_t point_shadow_map_binding_0 = 15;

        // std140 layout of the PointShadow block: mat4 faceViewProj[6] at
        // offset 0 (384 bytes), vec4 lightPos at 384, vec4 params at 400
        // (x enabled, y bias, z caster point index). 416 bytes total.
        constexpr size_t point_shadow_ubo_size =
            point_shadow_face_count * sizeof(core::math::mat4) + 2 * 4 * sizeof(float);
    } // namespace

    scene_pass::scene_pass(std::vector<renderable*>* registry,
                           shadow_pass* shadow,
                           point_shadow_pass* point_shadow,
                           render_stats* stats)
        : m_registry(registry), m_shadow(shadow), m_point_shadow(point_shadow), m_stats(stats)
    {
        auto& gpu = *runtime::current_engine().gpu;

        gpu::bind_group_layout_descriptor frame_layout_descriptor{};
        frame_layout_descriptor.entries.push_back({camera_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({lights_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({shadow_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({shadow_map_binding, gpu::binding_kind::texture});
        frame_layout_descriptor.entries.push_back({point_shadow_binding, gpu::binding_kind::uniform_buffer});
        for (int face = 0; face < point_shadow_face_count; ++face)
        {
            frame_layout_descriptor.entries.push_back(
                {point_shadow_map_binding_0 + static_cast<uint32_t>(face), gpu::binding_kind::texture});
        }
        m_frame_layout = gpu.create_bind_group_layout(frame_layout_descriptor);

        gpu::buffer_descriptor ubo_descriptor{};
        ubo_descriptor.size = per_frame_ubo_size;
        ubo_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        ubo_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_frame_ubo = gpu.create_buffer(ubo_descriptor);

        gpu::buffer_descriptor lights_descriptor{};
        lights_descriptor.size = sizeof(gpu_lights);
        lights_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        lights_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_lights_ubo = gpu.create_buffer(lights_descriptor);

        gpu::buffer_descriptor shadow_descriptor{};
        shadow_descriptor.size = shadow_ubo_size;
        shadow_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        shadow_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_shadow_ubo = gpu.create_buffer(shadow_descriptor);

        gpu::buffer_descriptor point_shadow_descriptor{};
        point_shadow_descriptor.size = point_shadow_ubo_size;
        point_shadow_descriptor.usage = gpu::buffer_usage_uniform | gpu::buffer_usage_copy_dst;
        point_shadow_descriptor.hint = gpu::buffer_usage_hint::dynamic_data;
        m_point_shadow_ubo = gpu.create_buffer(point_shadow_descriptor);

        gpu::bind_group_descriptor frame_bind_group_descriptor{};
        frame_bind_group_descriptor.layout = m_frame_layout;

        gpu::binding_value camera_slot{};
        camera_slot.binding = camera_binding;
        camera_slot.kind = gpu::binding_kind::uniform_buffer;
        camera_slot.buffer_value = m_frame_ubo;
        frame_bind_group_descriptor.entries.push_back(camera_slot);

        gpu::binding_value lights_slot{};
        lights_slot.binding = lights_binding;
        lights_slot.kind = gpu::binding_kind::uniform_buffer;
        lights_slot.buffer_value = m_lights_ubo;
        frame_bind_group_descriptor.entries.push_back(lights_slot);

        gpu::binding_value shadow_slot{};
        shadow_slot.binding = shadow_binding;
        shadow_slot.kind = gpu::binding_kind::uniform_buffer;
        shadow_slot.buffer_value = m_shadow_ubo;
        frame_bind_group_descriptor.entries.push_back(shadow_slot);

        // The shadow map is owned by the shadow pass. Its handle is
        // stable across frames, so capture it once here; an invalid
        // handle (no shadow pass) simply binds nothing.
        gpu::binding_value shadow_map_slot{};
        shadow_map_slot.binding = shadow_map_binding;
        shadow_map_slot.kind = gpu::binding_kind::texture;
        shadow_map_slot.texture_value = m_shadow != nullptr ? m_shadow->shadow_map() : gpu::texture{};
        frame_bind_group_descriptor.entries.push_back(shadow_map_slot);

        gpu::binding_value point_shadow_slot{};
        point_shadow_slot.binding = point_shadow_binding;
        point_shadow_slot.kind = gpu::binding_kind::uniform_buffer;
        point_shadow_slot.buffer_value = m_point_shadow_ubo;
        frame_bind_group_descriptor.entries.push_back(point_shadow_slot);

        // The six omni face maps are owned by the point shadow pass; their
        // handles are stable across frames. An invalid handle (no pass) binds
        // nothing and the lit shader's enabled flag keeps it unsampled.
        for (int face = 0; face < point_shadow_face_count; ++face)
        {
            gpu::binding_value point_map_slot{};
            point_map_slot.binding = point_shadow_map_binding_0 + static_cast<uint32_t>(face);
            point_map_slot.kind = gpu::binding_kind::texture;
            point_map_slot.texture_value =
                m_point_shadow != nullptr ? m_point_shadow->shadow_map(face) : gpu::texture{};
            frame_bind_group_descriptor.entries.push_back(point_map_slot);
        }

        m_frame_bind_group = gpu.create_bind_group(frame_bind_group_descriptor);
    }

    scene_pass::~scene_pass()
    {
        auto& gpu = *runtime::current_engine().gpu;
        if (m_frame_bind_group.valid())
        {
            gpu.destroy(m_frame_bind_group);
            m_frame_bind_group = {};
        }
        if (m_point_shadow_ubo.valid())
        {
            gpu.destroy(m_point_shadow_ubo);
            m_point_shadow_ubo = {};
        }
        if (m_shadow_ubo.valid())
        {
            gpu.destroy(m_shadow_ubo);
            m_shadow_ubo = {};
        }
        if (m_lights_ubo.valid())
        {
            gpu.destroy(m_lights_ubo);
            m_lights_ubo = {};
        }
        if (m_frame_ubo.valid())
        {
            gpu.destroy(m_frame_ubo);
            m_frame_ubo = {};
        }
        if (m_frame_layout.valid())
        {
            gpu.destroy(m_frame_layout);
            m_frame_layout = {};
        }
    }

    gpu::bind_group_layout scene_pass::frame_bind_group_layout() const
    {
        return m_frame_layout;
    }

    gpu::bind_group scene_pass::frame_bind_group() const
    {
        return m_frame_bind_group;
    }

    void scene_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        auto& eng = runtime::current_engine();
        auto& gpu = *eng.gpu;

        // Reset this frame's stats up front. The renderable count is known
        // regardless of whether a camera is attached; the draw totals stay
        // zero on no-camera frames (nothing is collected below).
        if (m_stats != nullptr)
        {
            *m_stats = render_stats{};
            m_stats->scene_renderables = static_cast<uint32_t>(m_registry->size());
        }

        // Render into the HDR scene-colour target so the post chain
        // can sample real luminance. The tonemap post pass maps the
        // result onto the swapchain before the UI composites.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = ctx.scene_color_target;
        descriptor.color.load = gpu::load_op::clear;
        descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::clear;
        descriptor.depth.clear_depth = 1.0f;

        auto pass_encoder = encoder.begin_render_pass(descriptor);

        // No camera, no scene — but we still opened the pass so the
        // HDR target gets cleared to black. Otherwise the tonemap
        // would map stale or driver-uninitialised contents into the
        // swapchain on no-camera frames.
        if (ctx.active_camera == nullptr)
        {
            pass_encoder->end();
            return;
        }

        // Refill the per-frame UBO before any draw consults it.
        // Layout matches the GLSL @c PerFrame block: viewMatrix at
        // offset 0, projectionMatrix at offset sizeof(mat4), then the
        // fog block (fogColor at float 32, fogParams at float 36).
        std::array<float, 40> ubo_payload{};
        const auto view = ctx.active_camera->get_view_matrix();
        const auto projection = ctx.active_camera->get_projection_matrix();
        std::memcpy(ubo_payload.data(), view.data(), sizeof(core::math::mat4));
        std::memcpy(ubo_payload.data() + 16, projection.data(), sizeof(core::math::mat4));
        // fogColor.rgb + fogColor.a = mode (0 none, 1 linear, 2 exp2);
        // the lit shaders skip the blend when the mode is 0.
        ubo_payload[32] = ctx.fog.color.x;
        ubo_payload[33] = ctx.fog.color.y;
        ubo_payload[34] = ctx.fog.color.z;
        ubo_payload[35] = static_cast<float>(static_cast<int>(ctx.fog.mode));
        ubo_payload[36] = ctx.fog.near_distance;
        ubo_payload[37] = ctx.fog.far_distance;
        ubo_payload[38] = ctx.fog.density;
        gpu.write_buffer(m_frame_ubo, ubo_payload.data(), per_frame_ubo_size, 0);

        // Pack every live light into the std140 lights block and upload
        // it alongside the camera UBO. Lit materials read this from the
        // per-frame group; the scene pass owns it so light objects never
        // touch the GPU directly.
        gpu_lights lights_payload{};
        pack_lights(registered_lights(), lights_payload);
        gpu.write_buffer(m_lights_ubo, &lights_payload, sizeof(gpu_lights), 0);

        // Upload the directional shadow block: the light-space matrix
        // plus {enabled, bias, caster index}. When no caster is active
        // the enabled flag stays 0 and the lit shader skips sampling,
        // so the matrix and the (cleared) map go unused.
        std::array<float, 20> shadow_payload{};
        if (m_shadow != nullptr && m_shadow->has_shadow())
        {
            std::memcpy(shadow_payload.data(), m_shadow->light_view_projection().data(), sizeof(core::math::mat4));
            shadow_payload[16] = 1.0f;
            shadow_payload[17] = m_shadow->depth_bias();
            shadow_payload[18] = static_cast<float>(m_shadow->shadow_light_index());
        }
        gpu.write_buffer(m_shadow_ubo, shadow_payload.data(), shadow_ubo_size, 0);

        // Upload the omni shadow block: six face matrices, the light position,
        // and {enabled, bias, caster point index}. enabled stays 0 with no
        // caster so the lit shader skips the (cleared) maps.
        std::array<float, 104> point_shadow_payload{};
        if (m_point_shadow != nullptr && m_point_shadow->has_shadow())
        {
            for (int face = 0; face < point_shadow_face_count; ++face)
            {
                std::memcpy(point_shadow_payload.data() + face * 16,
                            m_point_shadow->light_view_projection(face).data(),
                            sizeof(core::math::mat4));
            }
            const auto& pos = m_point_shadow->light_position();
            point_shadow_payload[96] = pos.x;
            point_shadow_payload[97] = pos.y;
            point_shadow_payload[98] = pos.z;
            point_shadow_payload[100] = 1.0f; // enabled
            point_shadow_payload[101] = m_point_shadow->depth_bias();
            point_shadow_payload[102] = static_cast<float>(m_point_shadow->shadow_point_index());
        }
        gpu.write_buffer(m_point_shadow_ubo, point_shadow_payload.data(), point_shadow_ubo_size, 0);

        // Collect every renderable's draw items into a single per-frame
        // list, then sort by pipeline id so the dispatch loop only
        // calls @c set_pipeline when the active material changes. The
        // sort is stable so submission order is preserved within a
        // material — important for any future renderable that relies on
        // back-to-front draw order.
        m_items.clear();
        for (auto* r : *m_registry)
        {
            r->collect_draw_items(m_items);
        }
        std::stable_sort(m_items.begin(),
                         m_items.end(),
                         [](const draw_item& a, const draw_item& b)
                         { return a.mat->pipeline().id < b.mat->pipeline().id; });

        // Tally this frame's draw statistics for the debug overlay. Each
        // item is one draw call; triangle / vertex counts scale by the
        // item's instance count. Indexed draws count index_count vertices
        // (vertices fetched), non-indexed count vertex_count.
        if (m_stats != nullptr)
        {
            m_stats->draw_calls = static_cast<uint32_t>(m_items.size());
            for (const auto& item : m_items)
            {
                const uint32_t instances = item.instance_count == 0 ? 1u : item.instance_count;
                const uint32_t verts = item.index_buffer.valid() ? item.index_count : item.vertex_count;
                m_stats->instances += instances;
                m_stats->vertices += static_cast<uint64_t>(verts) * instances;
                m_stats->triangles += static_cast<uint64_t>(verts / 3u) * instances;
            }
        }

        uint64_t last_pipeline_id = 0;
        bool first_iter = true;
        for (const auto& item : m_items)
        {
            const uint64_t pid = item.mat->pipeline().id;
            if (pid != last_pipeline_id)
            {
                pass_encoder->set_pipeline(item.mat->pipeline());

                // Per-frame bind group bound once per frame after
                // the first pipeline change; the binding sticks
                // across subsequent set_pipeline calls within the
                // same pass.
                if (first_iter)
                {
                    pass_encoder->set_bind_group(0, m_frame_bind_group);
                    first_iter = false;
                }

                if (item.mat->per_material_bind_group().valid())
                {
                    pass_encoder->set_bind_group(item.mat->per_material_slot(), item.mat->per_material_bind_group());
                }
                last_pipeline_id = pid;
            }

            // Instanced renderables keep their per-instance data in a
            // vertex stream (slot 1), not a per-draw bind group, so the
            // per-draw group is optional.
            if (item.per_draw_bind_group.valid())
            {
                pass_encoder->set_bind_group(item.mat->per_draw_slot(), item.per_draw_bind_group);
            }
            pass_encoder->set_vertex_buffer(0, item.vertex_buffer, 0, item.vertex_stride);
            if (item.instance_buffer.valid())
            {
                pass_encoder->set_vertex_buffer(1, item.instance_buffer, 0, item.instance_stride);
            }
            if (item.index_buffer.valid())
            {
                pass_encoder->set_index_buffer(item.index_buffer, item.index_format);
                if (item.indirect_buffer.valid())
                {
                    // Instanced draw: index and instance counts come from
                    // the indirect command record (see @ref instanced_mesh).
                    pass_encoder->draw_indexed_indirect(item.indirect_buffer, 0);
                }
                else
                {
                    pass_encoder->draw_indexed(item.index_count, 0);
                }
            }
            else
            {
                pass_encoder->draw(item.vertex_count, 0);
            }
        }

        eng.events->emit<core::render_scene>(pass_encoder.get());
        pass_encoder->end();
    }
} // namespace rendering_engine
