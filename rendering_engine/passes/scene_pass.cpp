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

#include <control/engine.hpp>
#include <event_engine/event.hpp>
#include <event_engine/event_engine.hpp>
#include <infrastructure/math/math.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/gpu/buffer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/lighting/light.hpp>
#include <rendering_engine/lighting/lights_ubo.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/passes/shadow_pass.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    namespace
    {
        // std140 layout for the per-frame camera UBO: mat4 viewMatrix
        // at offset 0, mat4 projectionMatrix at offset 64. mat4 is
        // 16-byte aligned and 64 bytes; no padding needed between
        // the two members.
        constexpr size_t per_frame_ubo_size = 2 * sizeof(infrastructure::math::mat4);

        // Binding numbers within the per-frame bind group (slot 0). The
        // numbers must stay unique across both descriptor sets in a lit
        // pipeline because OpenGL flattens UBO bindings into a single
        // namespace through ARB_gl_spirv: binding 0 = camera here,
        // binding 1 = the per-draw model matrix (set 1), so the lights
        // block takes binding 2.
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
        constexpr size_t shadow_ubo_size = sizeof(infrastructure::math::mat4) + 4 * sizeof(float);
    } // namespace

    scene_pass::scene_pass(std::vector<renderable*>* registry, shadow_pass* shadow)
        : m_registry(registry), m_shadow(shadow)
    {
        auto& gpu = *control::current_engine().gpu;

        gpu::bind_group_layout_descriptor frame_layout_descriptor{};
        frame_layout_descriptor.entries.push_back({camera_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({lights_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({shadow_binding, gpu::binding_kind::uniform_buffer});
        frame_layout_descriptor.entries.push_back({shadow_map_binding, gpu::binding_kind::texture});
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

        m_frame_bind_group = gpu.create_bind_group(frame_bind_group_descriptor);
    }

    scene_pass::~scene_pass()
    {
        auto& gpu = *control::current_engine().gpu;
        if (m_frame_bind_group.valid())
        {
            gpu.destroy(m_frame_bind_group);
            m_frame_bind_group = {};
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

    void scene_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        auto& eng = control::current_engine();
        auto& gpu = *eng.gpu;

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
        // offset 0, projectionMatrix at offset sizeof(mat4).
        std::array<float, 32> ubo_payload{};
        const auto view = ctx.active_camera->get_view_matrix();
        const auto projection = ctx.active_camera->get_projection_matrix();
        std::memcpy(ubo_payload.data(), view.data(), sizeof(infrastructure::math::mat4));
        std::memcpy(ubo_payload.data() + 16, projection.data(), sizeof(infrastructure::math::mat4));
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
            std::memcpy(
                shadow_payload.data(), m_shadow->light_view_projection().data(), sizeof(infrastructure::math::mat4));
            shadow_payload[16] = 1.0f;
            shadow_payload[17] = m_shadow->depth_bias();
            shadow_payload[18] = static_cast<float>(m_shadow->shadow_light_index());
        }
        gpu.write_buffer(m_shadow_ubo, shadow_payload.data(), shadow_ubo_size, 0);

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

            pass_encoder->set_bind_group(item.mat->per_draw_slot(), item.per_draw_bind_group);
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

        eng.events->emit<event_engine::render_scene>(pass_encoder.get());
        pass_encoder->end();
    }
} // namespace rendering_engine
