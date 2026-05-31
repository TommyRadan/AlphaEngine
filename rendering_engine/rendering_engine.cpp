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
#include <rendering_engine/materials/basic_material.hpp>
#include <rendering_engine/materials/ui_material.hpp>
#include <rendering_engine/passes/debug_pass.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/post/tonemap_pass.hpp>
#include <rendering_engine/passes/scene_pass.hpp>
#include <rendering_engine/passes/ui_pass.hpp>
#include <rendering_engine/renderables/renderable.hpp>
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
    uint32_t width = 0;
    uint32_t height = 0;
    if (eng.settings != nullptr)
    {
        width = eng.settings->get_window_width();
        height = eng.settings->get_window_height();
        eng.gpu->resize_swapchain(width, height);
    }

    // Allocate the off-screen HDR scene-colour target. The scene pass
    // renders into rgba16f instead of straight to the swapchain so
    // tonemap, bloom and any other post effect can sample real HDR
    // luminance. Sized at the current backbuffer; resize handling is
    // a follow-up.
    gpu::render_target_descriptor scene_color_descriptor{};
    scene_color_descriptor.color_format = gpu::texture_format::rgba16_float;
    scene_color_descriptor.width = width;
    scene_color_descriptor.height = height;
    scene_color_descriptor.with_depth = true;
    scene_color_descriptor.depth_format = gpu::texture_format::depth24;
    m_scene_color_target = eng.gpu->create_render_target(scene_color_descriptor);
    m_scene_color_texture = eng.gpu->render_target_color_texture(m_scene_color_target);

    // Construct the built-in passes first — each pass owns the
    // per-frame bind-group layout its matching material reads at
    // pipeline-create time.
    auto scene = std::make_unique<scene_pass>(&m_scene_renderables);
    const gpu::bind_group_layout scene_frame_layout = scene->frame_bind_group_layout();
    auto post = std::make_unique<tonemap_pass>(m_scene_color_texture);
    auto ui = std::make_unique<ui_pass>(&m_ui_renderables);
#if _DEBUG
    auto debug = std::make_unique<debug_pass>(&m_debug_renderables);
#endif

    // Construct the built-in materials against the per-frame layouts
    // exposed by the passes. The basic material's pipeline reserves
    // slot 0 for the scene_pass's per-frame group; the ui material
    // has no per-frame group.
    m_basic_material = std::make_unique<basic_material>(scene_frame_layout);
    m_ui_material = std::make_unique<ui_material>();
    LOG_INF("Rendering Engine: basic_material and ui_material constructed");

    // Register the built-in passes in render order: scene writes into
    // the HDR target, the tonemap post pass maps it to LDR on the
    // swapchain, and the UI pass composites on top. The debug pass is
    // appended in debug builds only so debug visuals always read on
    // top of the game UI; release builds drop it entirely so the
    // overlay registry has no consumer and the stage costs nothing.
    // Future post effects insert between scene and ui by pushing into
    // this list; future debug consumers (wireframe, gizmos, frustum
    // visualisations) register with the debug-renderable registry
    // rather than adding new passes.
    m_passes.push_back(std::move(scene));
    m_passes.push_back(std::move(post));
    m_passes.push_back(std::move(ui));
#if _DEBUG
    m_passes.push_back(std::move(debug));
#endif
}

void rendering_engine::context::quit()
{
    auto& eng = control::current_engine();

    // Drop the passes first; their record() bodies reach for the
    // event bus we're about to release, and the passes own per-frame
    // bind-group layouts referenced by the materials' pipelines.
    m_passes.clear();

    // Then materials, which own pipelines that reference the device.
    // Release them before the device tears its pools down.
    m_ui_material.reset();
    m_basic_material.reset();

    // Release the off-screen HDR target before the device tears its
    // pools down. The colour and depth attachments are owned by the
    // target so destroy() releases all three.
    if (m_scene_color_target.valid())
    {
        eng.gpu->destroy(m_scene_color_target);
        m_scene_color_target = {};
        m_scene_color_texture = {};
    }

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
    frame_context ctx{
        gpu.swapchain_target(), camera::get_current_camera(), m_scene_color_target, m_scene_color_texture};

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

void rendering_engine::context::register_debug_renderable(renderable* r)
{
    if (r != nullptr)
    {
        m_debug_renderables.push_back(r);
    }
}

void rendering_engine::context::unregister_debug_renderable(renderable* r)
{
    m_debug_renderables.erase(std::remove(m_debug_renderables.begin(), m_debug_renderables.end(), r),
                              m_debug_renderables.end());
}

rendering_engine::basic_material& rendering_engine::context::get_basic_material()
{
    return *m_basic_material;
}

rendering_engine::ui_material& rendering_engine::context::get_ui_material()
{
    return *m_ui_material;
}
