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

#include <rendering_engine/rendering_engine.hpp>

#include <control/engine.hpp>
#include <event_engine/event_engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/render_target.hpp>
#include <rendering_engine/renderers/basic_renderer.hpp>
#include <rendering_engine/renderers/overlay_renderer.hpp>
#include <rendering_engine/window.hpp>

void rendering_engine::context::init()
{
    LOG_INF("Init Rendering Engine");

    auto& eng = control::current_engine();
    eng.window->init();
    eng.gpu->init();

    // Tell the device about the initial backbuffer dimensions so that
    // begin_render_pass can default the viewport to the full window.
    if (eng.settings != nullptr)
    {
        eng.gpu->resize_swapchain(eng.settings->get_window_width(), eng.settings->get_window_height());
    }

    // GL is live; construct the built-in renderers so their pipelines
    // can be built against the active context.
    eng.basic_renderer = std::make_unique<rendering_engine::renderers::basic_renderer>();
    eng.overlay_renderer = std::make_unique<rendering_engine::renderers::overlay_renderer>();
    LOG_INF("Rendering Engine: basic_renderer and overlay_renderer constructed");
}

void rendering_engine::context::quit()
{
    auto& eng = control::current_engine();

    // The renderers own pipelines that reference the device — release
    // them before the device tears its pools down.
    eng.overlay_renderer.reset();
    eng.basic_renderer.reset();

    eng.gpu->quit();
    eng.window->quit();

    LOG_INF("Quit Rendering Engine");
}

void rendering_engine::context::render()
{
    auto& eng = control::current_engine();
    auto& gpu = *eng.gpu;

    auto encoder = gpu.create_command_encoder();

    if (rendering_engine::camera::get_current_camera() != nullptr)
    {
        rendering_engine::gpu::render_pass_descriptor scene_pass{};
        scene_pass.target = gpu.swapchain_target();
        scene_pass.color.load = rendering_engine::gpu::load_op::clear;
        scene_pass.color.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        scene_pass.use_depth = true;
        scene_pass.depth.load = rendering_engine::gpu::load_op::clear;
        scene_pass.depth.clear_depth = 1.0f;

        auto pass = encoder->begin_render_pass(scene_pass);
        eng.basic_renderer->begin(*pass);
        eng.events->emit<event_engine::render_scene>(pass.get());
        eng.basic_renderer->end(*pass);
        pass->end();
    }

    {
        rendering_engine::gpu::render_pass_descriptor ui_pass{};
        ui_pass.target = gpu.swapchain_target();
        // The scene pass already cleared the framebuffer (or there was
        // no camera and we're drawing UI on a fresh black backbuffer);
        // either way the UI overlay is drawn on top without re-clearing
        // the colour, but we do clear the depth so 3D content from the
        // previous pass doesn't reject overlay fragments.
        ui_pass.color.load = rendering_engine::gpu::load_op::load;
        ui_pass.use_depth = false;

        auto pass = encoder->begin_render_pass(ui_pass);
        eng.overlay_renderer->begin(*pass);
        eng.events->emit<event_engine::render_ui>(pass.get());
        eng.overlay_renderer->end(*pass);
        pass->end();
    }

    gpu.submit(std::move(encoder));
}
