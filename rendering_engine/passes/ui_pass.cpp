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

#include <rendering_engine/passes/ui_pass.hpp>

#include <control/engine.hpp>
#include <event_engine/event.hpp>
#include <event_engine/event_engine.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/renderers/overlay_renderer.hpp>

namespace rendering_engine
{
    ui_pass::ui_pass(std::vector<renderable*>* registry) : m_registry(registry) {}

    void ui_pass::record(gpu::command_encoder& encoder, const frame_context& ctx)
    {
        gpu::render_pass_descriptor descriptor{};
        descriptor.target = ctx.swapchain_target;
        // The scene pass already cleared the framebuffer (or there
        // was no camera and we're drawing UI on a fresh black
        // backbuffer); either way the UI overlay is drawn on top
        // without re-clearing the colour, and depth is disabled so
        // the overlay always wins.
        descriptor.color.load = gpu::load_op::load;
        descriptor.use_depth = false;

        auto& eng = control::current_engine();

        auto pass_encoder = encoder.begin_render_pass(descriptor);
        eng.overlay_renderer->begin(*pass_encoder);
        for (auto* r : *m_registry)
        {
            r->render(*pass_encoder);
        }
        eng.events->emit<event_engine::render_ui>(pass_encoder.get());
        eng.overlay_renderer->end(*pass_encoder);
        pass_encoder->end();
    }
} // namespace rendering_engine
