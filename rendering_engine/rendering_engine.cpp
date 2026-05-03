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
#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <rendering_engine/camera/camera.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/scene_pass.hpp>
#include <rendering_engine/passes/ui_pass.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/renderers/basic_renderer.hpp>
#include <rendering_engine/renderers/overlay_renderer.hpp>
#include <rendering_engine/window.hpp>

#include <algorithm>

rendering_engine::context::context() = default;
rendering_engine::context::~context() = default;

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

    // Register the built-in passes in render order. Future passes
    // (post-process, shadow, depth pre-pass, debug overlay) push
    // into this list at startup instead of editing render().
    m_passes.push_back(std::make_unique<scene_pass>(&m_scene_renderables));
    m_passes.push_back(std::make_unique<ui_pass>(&m_ui_renderables));
}

void rendering_engine::context::quit()
{
    auto& eng = control::current_engine();

    // Drop the passes first; their record() bodies reach for the
    // renderers and event bus we're about to release.
    m_passes.clear();

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

    // Capture per-frame state once so passes cannot disagree about
    // which camera or backbuffer is active mid-frame, and so they
    // do not have to re-query the camera singleton on every entry.
    frame_context ctx{gpu.swapchain_target(), camera::get_current_camera()};

    auto encoder = gpu.create_command_encoder();
    for (auto& p : m_passes)
    {
        p->record(*encoder, ctx);
    }
    gpu.submit(std::move(encoder));
}

void rendering_engine::context::register_scene_renderable(renderable* r)
{
    if (r != nullptr)
    {
        m_scene_renderables.push_back(r);
    }
}

void rendering_engine::context::unregister_scene_renderable(renderable* r)
{
    m_scene_renderables.erase(std::remove(m_scene_renderables.begin(), m_scene_renderables.end(), r),
                              m_scene_renderables.end());
}

void rendering_engine::context::register_ui_renderable(renderable* r)
{
    if (r != nullptr)
    {
        m_ui_renderables.push_back(r);
    }
}

void rendering_engine::context::unregister_ui_renderable(renderable* r)
{
    m_ui_renderables.erase(std::remove(m_ui_renderables.begin(), m_ui_renderables.end(), r), m_ui_renderables.end());
}
