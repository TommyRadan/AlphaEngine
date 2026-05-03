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

#include <control/engine.hpp>
#include <event_engine/event.hpp>
#include <event_engine/event_engine.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    scene_pass::scene_pass(std::vector<renderable*>* registry) : m_registry(registry)
    {
        auto& gpu = *control::current_engine().gpu;

        gpu::bind_group_layout_descriptor frame_layout_descriptor{};
        frame_layout_descriptor.entries.push_back({0, gpu::binding_kind::mat4_value, "viewMatrix"});
        frame_layout_descriptor.entries.push_back({1, gpu::binding_kind::mat4_value, "projectionMatrix"});
        m_frame_layout = gpu.create_bind_group_layout(frame_layout_descriptor);

        gpu::bind_group_descriptor frame_bind_group_descriptor{};
        frame_bind_group_descriptor.layout = m_frame_layout;
        gpu::binding_value view_slot{};
        view_slot.binding = 0;
        view_slot.kind = gpu::binding_kind::mat4_value;
        gpu::binding_value projection_slot{};
        projection_slot.binding = 1;
        projection_slot.kind = gpu::binding_kind::mat4_value;
        frame_bind_group_descriptor.entries.push_back(view_slot);
        frame_bind_group_descriptor.entries.push_back(projection_slot);
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
        // can sample real luminance. The passthrough post pass copies
        // the result into the swapchain before the UI composites.
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = ctx.scene_color_target;
        descriptor.color.load = gpu::load_op::clear;
        descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::clear;
        descriptor.depth.clear_depth = 1.0f;

        auto pass_encoder = encoder.begin_render_pass(descriptor);

        // No camera, no scene — but we still opened the pass so the
        // HDR target gets cleared to black. Otherwise the passthrough
        // would copy stale or driver-uninitialised contents into the
        // swapchain on no-camera frames.
        if (ctx.active_camera == nullptr)
        {
            pass_encoder->end();
            return;
        }

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

                // Per-frame bind group must be bound after a pipeline
                // is active (the GL backend caches uniform locations
                // against the current program). Update the camera
                // matrices and bind the group on the first pipeline
                // change of the frame; the binding sticks across
                // subsequent set_pipeline calls within the same pass.
                if (first_iter)
                {
                    std::vector<gpu::binding_value> entries;
                    entries.reserve(2);
                    gpu::binding_value view{};
                    view.binding = 0;
                    view.kind = gpu::binding_kind::mat4_value;
                    view.mat4_value = ctx.active_camera->get_view_matrix();
                    entries.push_back(view);
                    gpu::binding_value projection{};
                    projection.binding = 1;
                    projection.kind = gpu::binding_kind::mat4_value;
                    projection.mat4_value = ctx.active_camera->get_projection_matrix();
                    entries.push_back(projection);
                    gpu.update_bind_group(m_frame_bind_group, entries);
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
