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

#include <control/engine.hpp>
#include <event_engine/event.hpp>
#include <event_engine/event_engine.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/renderers/basic_renderer.hpp>

namespace rendering_engine
{
    scene_pass::scene_pass(std::vector<renderable*>* registry) : m_registry(registry) {}

    void scene_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        // No camera, no scene. The UI pass still runs against the
        // raw backbuffer.
        if (ctx.active_camera == nullptr)
        {
            return;
        }

        gpu::render_pass_descriptor descriptor{};
        descriptor.target = ctx.swapchain_target;
        descriptor.color.load = gpu::load_op::clear;
        descriptor.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        descriptor.use_depth = true;
        descriptor.depth.load = gpu::load_op::clear;
        descriptor.depth.clear_depth = 1.0f;

        auto& eng = control::current_engine();

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        eng.basic_renderer->begin(*pass_encoder);
        for (auto* r : *m_registry)
        {
            r->render(*pass_encoder);
        }
        eng.events->emit<event_engine::render_scene>(pass_encoder.get());
        eng.basic_renderer->end(*pass_encoder);
        pass_encoder->end();
    }
} // namespace rendering_engine
