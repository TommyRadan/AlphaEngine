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
#include <rendering_engine/debug/axes_helper.hpp>
#include <rendering_engine/debug/helper.hpp>
#include <rendering_engine/debug/infinite_grid.hpp>
#include <rendering_engine/debug_ui/imgui_layer.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/ibl/environment.hpp>
#include <rendering_engine/materials/basic_material.hpp>
#include <rendering_engine/materials/grid_material.hpp>
#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/materials/points_material.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/materials/ui_material.hpp>
#include <rendering_engine/passes/debug_pass.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/point_shadow_pass.hpp>
#include <rendering_engine/passes/post/bloom_pass.hpp>
#include <rendering_engine/passes/post/fxaa_pass.hpp>
#include <rendering_engine/passes/post/tonemap_pass.hpp>
#include <rendering_engine/passes/scene_pass.hpp>
#include <rendering_engine/passes/shadow_pass.hpp>
#include <rendering_engine/passes/skybox_pass.hpp>
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

    // Allocate the off-screen LDR target the tonemap pass resolves into
    // and the FXAA pass samples. The swapchain cannot be bound as a
    // shader input, so the final anti-aliasing pass reads its tonemapped
    // source from this rgba8 intermediate and writes to the swapchain.
    // No depth: the post chain runs depth-disabled.
    gpu::render_target_descriptor ldr_color_descriptor{};
    ldr_color_descriptor.color_format = gpu::texture_format::rgba8_unorm;
    ldr_color_descriptor.width = width;
    ldr_color_descriptor.height = height;
    ldr_color_descriptor.with_depth = false;
    m_ldr_color_target = eng.gpu->create_render_target(ldr_color_descriptor);
    m_ldr_color_texture = eng.gpu->render_target_color_texture(m_ldr_color_target);

    // Construct the built-in passes first — each pass owns the
    // per-frame bind-group layout its matching material reads at
    // pipeline-create time. The shadow pass is built before the scene
    // pass so the latter can bake the shadow map into its per-frame
    // bind group and query the light-space matrix each frame; it walks
    // the same scene-renderable registry.
    auto shadow = std::make_unique<shadow_pass>(&m_scene_renderables);
    // The omni shadow pass renders six depth faces from the first shadow-casting
    // point light; like the directional shadow it runs before the scene pass so
    // its maps are ready for the per-frame bind group.
    auto point_shadow = std::make_unique<point_shadow_pass>(&m_scene_renderables);
    auto scene = std::make_unique<scene_pass>(&m_scene_renderables, shadow.get(), point_shadow.get());
    const gpu::bind_group_layout scene_frame_layout = scene->frame_bind_group_layout();
    // Kept so create_standard_material can build extra materials against
    // the same per-frame layout the scene pass binds at slot 0.
    m_scene_frame_layout = scene_frame_layout;
    // The skybox pass runs after the scene pass and composites the cube-map
    // background into the HDR target where no geometry was drawn. It stays
    // dormant until set_environment supplies a cube map.
    auto skybox = std::make_unique<skybox_pass>();
    m_skybox = skybox.get();
    // Bloom runs between the scene and tonemap passes: it reads the HDR
    // scene colour, blurs the bright pixels and additively composites the
    // glow back into the same target, so tonemap maps the bloomed result.
    auto bloom = std::make_unique<bloom_pass>(m_scene_color_texture, width, height);
    auto post = std::make_unique<tonemap_pass>(m_scene_color_texture);
    // FXAA closes the post chain: it samples the LDR target tonemap
    // resolved into and writes the anti-aliased result to the swapchain.
    auto fxaa = std::make_unique<fxaa_pass>(m_ldr_color_texture, width, height);
    auto ui = std::make_unique<ui_pass>(&m_ui_renderables);
#if _DEBUG
    // The debug pass binds the scene pass's per-frame camera group at
    // slot 0 so the line-based debug gizmos project with the same camera.
    auto debug = std::make_unique<debug_pass>(&m_debug_renderables, scene->frame_bind_group());
#endif

    // Construct the built-in materials against the per-frame layouts
    // exposed by the passes. The basic material's pipeline reserves
    // slot 0 for the scene_pass's per-frame group; the ui material
    // has no per-frame group.
    m_basic_material = std::make_unique<basic_material>(scene_frame_layout);
    m_phong_material = std::make_unique<phong_material>(scene_frame_layout);
    m_standard_material = std::make_unique<standard_material>(scene_frame_layout);
    m_points_material = std::make_unique<points_material>(scene_frame_layout);
    m_line_material = std::make_unique<line_material>(scene_frame_layout);
    // Depth-disabled line variant for the debug gizmos so they always
    // read on top in the depth-less debug pass.
    m_debug_line_material = std::make_unique<line_material>(scene_frame_layout, /*depth_tested=*/false);
    // Analytic infinite-grid material; shares the scene per-frame layout.
    m_grid_material = std::make_unique<grid_material>(scene_frame_layout);
    m_ui_material = std::make_unique<ui_material>();
    LOG_INF("Rendering Engine: basic_material, phong_material, standard_material, points_material, line_material and "
            "ui_material constructed");

    // Register the built-in passes in render order: scene writes into
    // the HDR target, the skybox pass fills the untouched background of
    // that target with the environment cube map, the bloom post pass
    // blurs its bright pixels back
    // into that target, the tonemap post pass maps the result to LDR in
    // the off-screen LDR target, the FXAA post pass anti-aliases that LDR
    // image onto the swapchain, and the UI pass composites on top. The
    // debug pass is appended in debug builds only so debug visuals read on
    // top of the game UI; release builds drop it entirely so the
    // overlay registry has no consumer and the stage costs nothing.
    // Further post effects insert between scene and ui by pushing into
    // this list; future debug consumers (wireframe, gizmos, frustum
    // visualisations) register with the debug-renderable registry
    // rather than adding new passes.
    // The shadow pass renders the light's depth map first so the scene
    // pass can sample it the same frame.
    m_passes.push_back(std::move(shadow));
    m_passes.push_back(std::move(point_shadow));
    m_passes.push_back(std::move(scene));
    m_passes.push_back(std::move(skybox));
    m_passes.push_back(std::move(bloom));
    m_passes.push_back(std::move(post));
    m_passes.push_back(std::move(fxaa));
    m_passes.push_back(std::move(ui));
#if _DEBUG
    m_passes.push_back(std::move(debug));
#endif

    // Bring the ImGui debug overlay up now that the window, GL context
    // and passes are live. No-op in release builds.
    debug_ui::init();

#if _DEBUG
    // Provide a couple of always-available reference gizmos (the infinite
    // ground grid + world axes) so a fresh debug build has something to
    // toggle from the overlay's Helpers panel. They auto-register into the
    // matching renderable registry and the helper registry on
    // construction: the infinite grid into the scene pass (depth-tested),
    // the axes into the always-on-top debug pass. Game code can add the
    // box / light / camera helpers against its own objects the same way.
    // The debug pass is dropped in release, so this whole block compiles
    // out there.
    m_debug_helpers.push_back(std::make_unique<debug::infinite_grid>());
    m_debug_helpers.push_back(std::make_unique<debug::axes_helper>());
#endif
}

void rendering_engine::context::quit()
{
    auto& eng = control::current_engine();

    // Tear the ImGui overlay down first, while the window and GL context
    // it bound to are still alive. No-op in release builds.
    debug_ui::shutdown();

    // Release the built-in debug helpers before the line material and the
    // GPU device they reference; their destructors unregister from the
    // debug-renderable registry and free their line buffers. Empty in
    // release. Game-owned helpers must likewise be released before quit.
    m_debug_helpers.clear();

    // Drop the passes first; their record() bodies reach for the
    // event bus we're about to release, and the passes own per-frame
    // bind-group layouts referenced by the materials' pipelines.
    m_passes.clear();
    m_skybox = nullptr;

    // Then materials, which own pipelines that reference the device.
    // Release them before the device tears its pools down.
    m_ui_material.reset();
    m_grid_material.reset();
    m_debug_line_material.reset();
    m_line_material.reset();
    m_points_material.reset();
    m_standard_material.reset();
    m_phong_material.reset();
    m_basic_material.reset();

    // Release the off-screen HDR target before the device tears its
    // pools down. The colour and depth attachments are owned by the
    // target so destroy() releases all three.
    if (m_ldr_color_target.valid())
    {
        eng.gpu->destroy(m_ldr_color_target);
        m_ldr_color_target = {};
        m_ldr_color_texture = {};
    }
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
    frame_context ctx{gpu.swapchain_target(),
                      camera::get_current_camera(),
                      m_scene_color_target,
                      m_scene_color_texture,
                      m_ldr_color_target,
                      m_ldr_color_texture};

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

rendering_engine::phong_material& rendering_engine::context::get_phong_material()
{
    return *m_phong_material;
}

rendering_engine::standard_material& rendering_engine::context::get_standard_material()
{
    return *m_standard_material;
}

rendering_engine::points_material& rendering_engine::context::get_points_material()
{
    return *m_points_material;
}

rendering_engine::line_material& rendering_engine::context::get_line_material()
{
    return *m_line_material;
}

rendering_engine::line_material& rendering_engine::context::get_debug_line_material()
{
    return *m_debug_line_material;
}

rendering_engine::grid_material& rendering_engine::context::get_grid_material()
{
    return *m_grid_material;
}

rendering_engine::ui_material& rendering_engine::context::get_ui_material()
{
    return *m_ui_material;
}

std::unique_ptr<rendering_engine::standard_material> rendering_engine::context::create_standard_material()
{
    auto material = std::make_unique<standard_material>(m_scene_frame_layout);
    if (m_environment != nullptr)
    {
        material->set_environment(*m_environment);
    }
    return material;
}

void rendering_engine::context::set_environment(const environment* env)
{
    m_environment = env;

    // Point the skybox pass at the cube map (or clear it) and mirror the
    // choice onto the built-in standard material so its surfaces pick up
    // the matching image-based ambient.
    if (m_skybox != nullptr)
    {
        m_skybox->set_cubemap(env != nullptr ? env->skybox() : gpu::texture{});
    }
    if (m_standard_material != nullptr)
    {
        if (env != nullptr)
        {
            m_standard_material->set_environment(*env);
        }
        else
        {
            m_standard_material->clear_environment();
        }
    }
}
