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

#include <rendering_engine/passes/debug_pass.hpp>

#include <algorithm>

#include <control/engine.hpp>
#include <event_engine/event.hpp>
#include <event_engine/event_engine.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/materials/material.hpp>
#include <rendering_engine/renderables/renderable.hpp>

namespace rendering_engine
{
    debug_pass::debug_pass(std::vector<renderable*>* registry) : m_registry(registry) {}

    void debug_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        auto& eng = control::current_engine();

        gpu::render_pass_descriptor descriptor{};
        descriptor.target = ctx.swapchain_target;
        // The UI pass already composited on top of the tonemapped
        // backbuffer; debug overlays paint on top of that without
        // re-clearing, and depth is disabled so they always win.
        descriptor.color.load = gpu::load_op::load;
        descriptor.use_depth = false;

        auto pass_encoder = encoder.begin_render_pass(descriptor);

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
        for (const auto& item : m_items)
        {
            const uint64_t pid = item.mat->pipeline().id;
            if (pid != last_pipeline_id)
            {
                pass_encoder->set_pipeline(item.mat->pipeline());
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

        eng.events->emit<event_engine::render_debug>(pass_encoder.get());
        pass_encoder->end();
    }
} // namespace rendering_engine
