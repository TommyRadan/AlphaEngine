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

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/settings.hpp>
#include <core/time.hpp>
#include <rendering_engine/camera/camera_registry.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/debug/axes_helper.hpp>
#include <rendering_engine/debug/helper.hpp>
#include <rendering_engine/debug/infinite_grid.hpp>
#include <rendering_engine/debug_ui/imgui_layer.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/ibl/environment.hpp>
#include <rendering_engine/materials/basic_material.hpp>
#include <rendering_engine/materials/grid_material.hpp>
#include <rendering_engine/materials/instanced_material.hpp>
#include <rendering_engine/materials/line_material.hpp>
#include <rendering_engine/materials/material_template.hpp>
#include <rendering_engine/materials/phong_material.hpp>
#include <rendering_engine/materials/points_material.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/materials/ui_material.hpp>
#include <rendering_engine/passes/debug_pass.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/point_shadow_pass.hpp>
#include <rendering_engine/passes/post/bloom_pass.hpp>
#include <rendering_engine/passes/post/fxaa_pass.hpp>
#include <rendering_engine/passes/post/taa_pass.hpp>
#include <rendering_engine/passes/post/tonemap_pass.hpp>
#include <rendering_engine/passes/post/velocity_pass.hpp>
#include <rendering_engine/passes/projection_jitter.hpp>
#include <rendering_engine/passes/scene_pass.hpp>
#include <rendering_engine/passes/shadow_pass.hpp>
#include <rendering_engine/passes/skybox_pass.hpp>
#include <rendering_engine/passes/spot_shadow_pass.hpp>
#include <rendering_engine/passes/ui_pass.hpp>
#include <rendering_engine/renderables/per_draw_ring.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/engine.hpp>

#include <algorithm>
#include <cassert>

rendering_engine::context::context() = default;
rendering_engine::context::~context() = default;

void rendering_engine::context::init()
{
    LOG_INF("Init Rendering Engine");

    auto& eng = runtime::current_engine();
    eng.window->init();
    eng.gpu->init();

    // The per-draw ring sizes its slots by the device's uniform-buffer
    // offset alignment, so it follows the device's init.
    m_per_draw_ring = std::make_unique<per_draw_ring>(*eng.gpu);

    // Tell the device about the initial backbuffer dimensions so that
    // begin_render_pass can default the viewport to the full window. The
    // drawable is measured in pixels rather than taken from the settings'
    // logical size: on a high-density display the two differ by the
    // display scale, and the swapchain and render targets follow pixels.
    const window_extent drawable = eng.window->pixel_size();
    const uint32_t width = drawable.width;
    const uint32_t height = drawable.height;
    eng.gpu->resize_swapchain(width, height);

    // Report the drawable's aspect to the camera registry: every attached
    // camera takes it now and any camera attached later takes it on
    // attach, so the projection always matches the drawable. The settings'
    // logical size stands in while the window has no drawable.
    set_drawable_aspect(drawable_aspect_ratio(width, height, eng.settings->window.aspect_ratio()));

    // Keep the swapchain extent, the off-screen targets and the passes in
    // step with the drawable as the window is resized, maximised, restored
    // or moved across displays. The listener runs from the window's event
    // pump in engine::tick, before the frame is built, so on_resize never
    // recreates a target a command buffer is being recorded against.
    m_window_resized_subscription = eng.events->subscribe<core::window_resized>(
        [this, &eng](const core::window_resized& e)
        {
            eng.gpu->resize_swapchain(e.m_pixel_width, e.m_pixel_height);
            on_resize(e.m_pixel_width, e.m_pixel_height);
        });

    // Allocate the off-screen HDR scene-colour target and the LDR target
    // at the current backbuffer size (on_resize recreates them later).
    create_color_targets(width, height);

    // Temporal AA is decided once, up front: it gates the projection jitter
    // the scene pass applies (and the unjittered overlay group it builds
    // for the debug pass), the velocity and TAA passes below, and which
    // input FXAA declares. Off when the setting is off or the drawable is
    // degenerate, in which case the LDR target flows straight into FXAA.
    const bool taa_enabled =
        (eng.settings != nullptr) && eng.settings->graphics.temporal_aa && width != 0 && height != 0;

    // Construct the built-in passes first — each pass owns the
    // per-frame bind-group layout its matching material reads at
    // pipeline-create time. The shadow pass is built before the scene
    // pass so the latter can bake the cascade array into its per-frame
    // bind group and query the cascade matrices each frame; it walks
    // the same scene-renderable registry. The shadow passes size their
    // maps and biases from the shadow settings, fixed at startup.
    const core::shadow_settings shadow_config =
        eng.settings != nullptr ? eng.settings->shadows : core::shadow_settings{};
    auto shadow = std::make_unique<shadow_pass>(&m_scene_renderables, shadow_config);
    m_shadow = shadow.get();
    // The omni shadow pass renders six depth faces from the first shadow-casting
    // point light; like the directional shadow it runs before the scene pass so
    // its maps are ready for the per-frame bind group.
    auto point_shadow = std::make_unique<point_shadow_pass>(&m_scene_renderables, shadow_config);
    // The spot shadow pass renders a single perspective depth map from the
    // first shadow-casting spot light; also runs before the scene pass.
    auto spot_shadow = std::make_unique<spot_shadow_pass>(&m_scene_renderables, shadow_config);
    m_spot_shadow = spot_shadow.get();
    auto scene = std::make_unique<scene_pass>(
        &m_scene_renderables, shadow.get(), point_shadow.get(), spot_shadow.get(), &m_render_stats, taa_enabled);
    // The material templates below are built against the same per-frame
    // layout the scene pass binds at slot 0.
    const gpu::bind_group_layout scene_frame_layout = scene->frame_bind_group_layout();
    // The skybox pass runs after the scene pass and composites the cube-map
    // background into the HDR target where no geometry was drawn. It stays
    // dormant until set_environment supplies a cube map.
    auto skybox = std::make_unique<skybox_pass>();
    m_skybox = skybox.get();
    // Bloom runs between the scene and tonemap passes: it reads the HDR
    // scene colour, blurs the bright pixels and additively composites the
    // glow back into the same target, so tonemap maps the bloomed result.
    // Neither pass takes the scene-colour texture here: both read it from
    // frame_context::scene_color_texture every frame and rebind when the
    // handle changes, so a resize that recreates the target reaches them
    // without re-plumbing.
    auto bloom = std::make_unique<bloom_pass>(width, height);
    auto post = std::make_unique<tonemap_pass>();
    m_tonemap = post.get();
    // Temporal AA optionally slots in between tonemap and FXAA: it
    // accumulates the projection-jittered frames the scene pass produces
    // (Halton sub-pixel offsets from frame_context::jitter, published only
    // while this is enabled) into a stable, supersampled LDR image, then
    // FXAA cleans up whatever spatial edges remain. The TAA resolve
    // becomes FXAA's input so the swapchain still receives a single
    // anti-aliased image. The textures the passes hand each other (scene
    // depth, motion vectors, the resolve) travel through frame_context
    // rather than constructor arguments: render() publishes them from the
    // owning pass each frame and the consumer rebinds when the handle
    // changes.
    std::unique_ptr<velocity_pass> velocity;
    std::unique_ptr<taa_pass> taa;
    if (taa_enabled)
    {
        // Per-pixel motion vectors are reconstructed from the scene depth
        // buffer: the velocity pass samples the HDR target's depth attachment
        // through frame_context::scene_depth_texture each frame. They drive
        // the TAA history reprojection.
        velocity = std::make_unique<velocity_pass>(width, height);
        taa = std::make_unique<taa_pass>(width, height);
        m_velocity = velocity.get();
        m_taa = taa.get();
    }
    // post_settings::taa.enabled mirrors the pass's real presence rather
    // than being requestable: temporal AA is only ever decided here, at
    // init, so set_post_settings overwrites whatever it is given with
    // this instead of trusting the caller.
    m_post_settings.taa.enabled = taa_enabled;
    // FXAA closes the post chain: it samples the TAA resolve when one is
    // published (else the LDR target) and writes the anti-aliased image to
    // the swapchain. It declares whichever of the two it will actually
    // read so the frame graph checks the real wiring.
    auto fxaa = std::make_unique<fxaa_pass>(width, height, taa_enabled);
    // The UI pass owns the pixel-space projection the ui template reads at
    // slot 0; it follows the drawable through pass::resize.
    auto ui = std::make_unique<ui_pass>(&m_ui_renderables, width, height);
    const gpu::bind_group_layout ui_frame_layout = ui->frame_bind_group_layout();
#if _DEBUG
    // The debug pass binds the scene pass's per-frame camera group at
    // slot 0 so the line-based debug gizmos project with the same camera.
    // It uses the unjittered overlay group: the debug pass paints after the
    // TAA resolve, so the projection jitter would otherwise show up as a
    // sub-pixel wobble on the gizmos rather than being averaged away.
    auto debug = std::make_unique<debug_pass>(&m_debug_renderables, scene->overlay_frame_bind_group());
#endif

    // Construct the built-in materials: one template per type (shaders,
    // layouts, the pipeline-variant cache) against the per-frame layouts
    // exposed by the passes, and the built-in instance of each. The 3D
    // templates reserve slot 0 for the scene_pass's per-frame group; the
    // ui template reserves it for the ui_pass's. Each instance keeps its
    // template alive; the standard template is also held here so
    // create_standard_material hands every extra instance the same one.
    gpu::device& device = *eng.gpu;
    m_basic_material = std::make_unique<basic_material>(basic_material::create_template(device, scene_frame_layout));
    m_instanced_material =
        std::make_unique<instanced_material>(instanced_material::create_template(device, scene_frame_layout));
    m_phong_material = std::make_unique<phong_material>(phong_material::create_template(device, scene_frame_layout));
    m_standard_template = standard_material::create_template(device, scene_frame_layout);
    m_standard_material = std::make_unique<standard_material>(m_standard_template);
    m_points_material = std::make_unique<points_material>(points_material::create_template(device, scene_frame_layout));
    // The scene lines and the depth-disabled debug-gizmo lines (which
    // always read on top in the depth-less debug pass) are two instances
    // of one line template, bound to two pipeline variants.
    const std::shared_ptr<material_template> line_template = line_material::create_template(device, scene_frame_layout);
    m_line_material = std::make_unique<line_material>(line_template);
    m_debug_line_material = std::make_unique<line_material>(line_template, /*depth_tested=*/false);
    // Analytic infinite-grid material; shares the scene per-frame layout.
    m_grid_material = std::make_unique<grid_material>(grid_material::create_template(device, scene_frame_layout));
    m_ui_material = std::make_unique<ui_material>(ui_material::create_template(device, ui_frame_layout));
    LOG_INF("Rendering Engine: basic_material, instanced_material, phong_material, standard_material, points_material, "
            "line_material and ui_material constructed");

    // Every built-in pipeline has compiled by now; report how much of it
    // the on-disk SPIR-V cache served (see gpu/shader_compiler.hpp).
    {
        const gpu::shader_cache_stats cache = gpu::shader_cache_statistics();
        LOG_DBG("Shader cache: %u hits, %u misses%s",
                cache.hits,
                cache.misses,
                gpu::shader_cache_directory().empty() ? " (cache disabled)" : "");
    }

    // Register the built-in passes in render order: scene writes into
    // the HDR target, the skybox pass fills the untouched background of
    // that target with the environment cube map, the optional velocity
    // pass reconstructs per-pixel motion vectors from the finalised depth
    // for the TAA reprojection, the bloom post pass blurs its bright pixels
    // back
    // into that target, the tonemap post pass maps the result to LDR in
    // the off-screen LDR target, the optional TAA post pass accumulates the
    // jittered LDR frames into a supersampled image, the FXAA post pass
    // anti-aliases that result onto the swapchain, and the UI pass
    // composites on top. The
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
    m_passes.push_back(std::move(spot_shadow));
    m_passes.push_back(std::move(scene));
    m_passes.push_back(std::move(skybox));
    // Motion vectors are computed from the finalised scene depth, before
    // the post chain consumes the colour, so the velocity pass sits right
    // after the geometry and skybox. Only present when TAA is enabled.
    if (velocity)
    {
        m_passes.push_back(std::move(velocity));
    }
    m_passes.push_back(std::move(bloom));
    m_passes.push_back(std::move(post));
    if (taa)
    {
        m_passes.push_back(std::move(taa));
    }
    m_passes.push_back(std::move(fxaa));
    m_passes.push_back(std::move(ui));
#if _DEBUG
    m_passes.push_back(std::move(debug));
#endif

    // Build the frame graph over the now-final pass list. The swapchain image
    // and, with temporal AA on, the TAA history are valid at frame start
    // without an in-frame producer, so import them as external; every other
    // resource is produced by a pass. Each pass declares its reads/writes
    // (those that override declare_io) and the graph validates the ordering.
    // Execution order is the m_passes order, so this does not change what is
    // rendered.
    m_frame_graph.import_external("swapchain");
    if (taa_enabled)
    {
        m_frame_graph.import_external("taa_history");
    }
    for (auto& p : m_passes)
    {
        render_graph::pass_io_builder io;
        p->declare_io(io);
        m_frame_graph.add_pass(p->name(),
                               std::move(io),
                               [raw = p.get()](gpu::command_encoder& encoder, const frame_context& frame)
                               { raw->record(encoder, frame); });
    }
    // A hazard is a pass reading a resource nothing before it produced: a
    // mis-ordered or mis-declared pass list, i.e. a programming error. It
    // stops a debug build here; a release build logs it (the graph already
    // reported each offending read) and renders in the declared order.
    const bool hazard_free = m_frame_graph.compile();
    assert(hazard_free && "frame graph: a pass reads a resource before any pass produces it");
    if (!hazard_free)
    {
        LOG_ERR("Rendering Engine: the frame graph compiled with hazards; the pass list is mis-declared or "
                "mis-ordered (see the frame_graph errors above)");
    }

    // Per-pass GPU timings over the compiled graph; disabled on a device
    // without timestamp queries.
    m_gpu_profiler.init(*eng.gpu, m_frame_graph.pass_names());

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
    auto& eng = runtime::current_engine();

    // Stop tracking window resizes before the device the listener
    // resizes goes away, and withdraw the drawable aspect the registry
    // hands to attaching cameras: there is no drawable to match now.
    m_window_resized_subscription.reset();
    set_drawable_aspect(0.0f);

    // Tear the ImGui overlay down first, while the window and GL context
    // it bound to are still alive. No-op in release builds.
    debug_ui::shutdown();

    // Release the built-in debug helpers before the line material and the
    // GPU device they reference; their destructors unregister from the
    // debug-renderable registry and free their line buffers. Empty in
    // release. Game-owned helpers must likewise be released before quit.
    m_debug_helpers.clear();

    // The profiler's query sets go before the device does.
    m_gpu_profiler.shutdown(*eng.gpu);

    // Drop the frame graph before the passes: its execute callbacks hold
    // raw pointers into m_passes.
    m_frame_graph.clear();

    // Drop the passes first; their record() bodies reach for the
    // event bus we're about to release, and the passes own per-frame
    // bind-group layouts referenced by the materials' pipelines.
    m_passes.clear();
    m_skybox = nullptr;
    m_tonemap = nullptr;
    m_velocity = nullptr;
    m_taa = nullptr;
    m_shadow = nullptr;
    m_spot_shadow = nullptr;
    m_prev_camera = nullptr;
    m_has_prev_view_projection = false;

    // Then materials, which own pipelines that reference the device.
    // Release them before the device tears its pools down.
    m_ui_material.reset();
    m_grid_material.reset();
    m_debug_line_material.reset();
    m_line_material.reset();
    m_points_material.reset();
    m_standard_material.reset();
    m_phong_material.reset();
    m_instanced_material.reset();
    m_basic_material.reset();
    // The other templates went with their last instance above; the
    // standard one is held here too and must go before the device.
    m_standard_template.reset();

    // The per-draw ring's buffers and shared groups go before the device.
    // Every renderable has released its per-draw state by now (they hold
    // no ring resources, only offsets).
    m_per_draw_ring.reset();

    // Release the off-screen HDR and LDR targets before the device tears
    // its pools down. The colour and depth attachments are owned by the
    // targets so destroy() releases them too.
    release_color_targets();

    eng.gpu->quit();
    eng.window->quit();

    LOG_INF("Quit Rendering Engine");
}

void rendering_engine::context::render()
{
    auto& eng = runtime::current_engine();
    auto& gpu = *eng.gpu;

    // Open the device frame before anything below touches GPU-visible
    // memory. A deferred-execution backend (Vulkan) blocks here until
    // the previous frame's command buffer has finished and then frees
    // the resources whose destruction it deferred while that buffer
    // could still reference them, so the per-frame UBO writes and
    // bind-group rebuilds the passes make during the walk never race
    // the GPU.
    gpu.begin_frame();
    m_in_frame = true;

    // Rewind the per-draw ring to this frame's region. It must follow
    // begin_frame: the region is rewritten from its first slot, which is
    // only safe once the frame that last read it has retired (see
    // per_draw_ring).
    m_per_draw_ring->begin_frame();

    // The previous frame's work has retired (or its queries are polled
    // without waiting), so its per-pass timestamps can be read now.
    m_gpu_profiler.resolve(gpu);

    // Capture per-frame state once so passes cannot disagree about
    // which camera or backbuffer is active mid-frame, and so they
    // do not have to re-run the camera arbitration on every entry.
    // active_camera() is the registry's pick for this frame: the
    // highest-priority attached, enabled camera.
    frame_context ctx{};
    ctx.swapchain_target = gpu.swapchain_target();
    ctx.active_camera = active_camera();
    ctx.viewport_width = m_target_width;
    ctx.viewport_height = m_target_height;
    ctx.frame_index = m_frame_index;
    // The engine clock ticked at the top of this frame; core::time reports
    // milliseconds, the shaders see seconds.
    if (eng.time != nullptr)
    {
        ctx.time_seconds = eng.time->total_time() / 1000.0f;
        ctx.delta_seconds = static_cast<float>(eng.time->delta_time() / 1000.0);
    }
    // The temporal-AA jitter is computed here from the live target size
    // (so a resize rescales it without any pass being told) and published
    // to every pass: the scene and skybox passes offset their projection
    // by it, the velocity pass subtracts it. Zero while TAA is off.
    ctx.jitter = (m_taa != nullptr) ? taa_jitter_ndc(m_frame_index, m_target_width, m_target_height)
                                    : core::math::vec2{0.0f, 0.0f};
    ctx.prev_jitter = m_prev_jitter;
    // The previous frame's unjittered view-projection is only meaningful
    // if that frame was drawn by this same camera.
    ctx.has_prev_view_projection =
        m_has_prev_view_projection && ctx.active_camera != nullptr && ctx.active_camera == m_prev_camera;
    ctx.prev_view_projection = ctx.has_prev_view_projection ? m_prev_view_projection : core::math::mat4{};
    ctx.scene_color_target = m_scene_color_target;
    ctx.scene_color_texture = m_scene_color_texture;
    // The depth attachment is looked up from the target every frame rather
    // than cached at init, so a resize that recreates the target hands the
    // new attachment to every depth consumer on the next frame.
    ctx.scene_depth_texture = gpu.render_target_depth_texture(m_scene_color_target);
    ctx.ldr_color_target = m_ldr_color_target;
    ctx.ldr_color_texture = m_ldr_color_texture;
    // The textures the temporal-AA passes own are published the same way:
    // read from the owning pass every frame so the TAA resolve and FXAA
    // rebind after a resize recreated them. Invalid while TAA is off.
    ctx.velocity_texture = (m_velocity != nullptr) ? m_velocity->velocity_texture() : gpu::texture{};
    ctx.taa_resolve_texture = (m_taa != nullptr) ? m_taa->output_texture() : gpu::texture{};
    ctx.fog = m_fog;
    ctx.post = m_post_settings;

    // One encoder records the frame graph's passes in order — each in a
    // debug group and between the profiler's timestamps — then submits.
    auto encoder = gpu.create_command_encoder();
    m_gpu_profiler.begin_frame(*encoder);
    m_frame_graph.execute(*encoder, ctx, &m_gpu_profiler);
    m_gpu_profiler.end_frame(*encoder);
    gpu.submit(std::move(encoder));

    // Carry this frame's camera state over for the next frame's
    // reprojection: the unjittered view-projection (the scene pass applies
    // the jitter on top of the camera's own matrices) and the camera it
    // came from. A no-camera frame leaves nothing to reproject against.
    if (ctx.active_camera != nullptr)
    {
        m_prev_view_projection = ctx.active_camera->get_projection_matrix() * ctx.active_camera->get_view_matrix();
        m_has_prev_view_projection = true;
    }
    else
    {
        m_has_prev_view_projection = false;
    }
    m_prev_camera = ctx.active_camera;
    m_prev_jitter = ctx.jitter;
    ++m_frame_index;

    // Close the frame. Vulkan presents the swapchain image it acquired
    // for this frame here; OpenGL presents when the main loop calls
    // window::swap_buffers.
    gpu.end_frame();
    m_in_frame = false;
}

void rendering_engine::context::on_resize(uint32_t pixel_width, uint32_t pixel_height)
{
    // The listener that calls this runs from the window's event pump,
    // never from inside render(): the targets released below may still be
    // bound to the frame being recorded otherwise.
    assert(!m_in_frame && "context::on_resize must not run while a frame is being recorded");

    // A zero dimension is a minimised window; the main loop skips whole
    // frames until it is restored (and the restore reports the real size),
    // so the targets keep their last usable size. A repeat of the live
    // size (the initial event, a DPI-only notification) changes nothing.
    if (pixel_width == 0 || pixel_height == 0)
    {
        return;
    }
    if (pixel_width == m_target_width && pixel_height == m_target_height)
    {
        return;
    }

    auto& eng = runtime::current_engine();
    auto& gpu = *eng.gpu;

    // Recreate the context-owned targets: new ones first, so every
    // consumer that compares the handle it bound against the one
    // frame_context publishes (tonemap, bloom, the velocity pass's depth,
    // the TAA resolve's LDR input) sees a different handle next frame;
    // then release the old ones. OpenGL frees them immediately, which is
    // fine between frames (its device drops its state cache on every
    // destroy and at every pass boundary, so nothing here has to reach
    // it); Vulkan defers the free until the last command buffer that
    // referenced them has retired.
    const gpu::render_target old_scene_color = m_scene_color_target;
    const gpu::render_target old_ldr_color = m_ldr_color_target;
    create_color_targets(pixel_width, pixel_height);
    if (old_ldr_color.valid())
    {
        gpu.destroy(old_ldr_color);
    }
    if (old_scene_color.valid())
    {
        gpu.destroy(old_scene_color);
    }

    // Let every pass follow: the bloom pyramid, the velocity target, the
    // TAA history / resolve (+ texel step, history reset), the FXAA edge
    // step and the UI's pixel-space projection. Fixed-size passes (shadow
    // maps, debug) keep the default no-op, and the scene pass needs
    // nothing: the jitter it applies is computed by render() from the
    // size recorded above.
    for (auto& p : m_passes)
    {
        p->resize(pixel_width, pixel_height);
    }

    // The projection follows the drawable so the image is not stretched:
    // the registry forwards the aspect to every attached camera and hands
    // it to any camera attached later. Both dimensions are non-zero here,
    // so the fallback is never used.
    set_drawable_aspect(drawable_aspect_ratio(pixel_width, pixel_height, 1.0f));

    LOG_INF("Rendering Engine: render targets resized to %ux%u", pixel_width, pixel_height);
}

void rendering_engine::context::create_color_targets(uint32_t width, uint32_t height)
{
    auto& gpu = *runtime::current_engine().gpu;

    // The HDR scene-colour target the scene pass renders into: rgba16f
    // instead of straight to the swapchain so tonemap, bloom and any
    // other post effect can sample real HDR luminance.
    gpu::render_target_descriptor scene_color_descriptor{};
    scene_color_descriptor.color = {{gpu::texture_format::rgba16_float}};
    scene_color_descriptor.width = width;
    scene_color_descriptor.height = height;
    scene_color_descriptor.with_depth = true;
    scene_color_descriptor.depth.format = gpu::texture_format::depth24;
    m_scene_color_target = gpu.create_render_target(scene_color_descriptor);
    m_scene_color_texture = gpu.render_target_color_texture(m_scene_color_target);

    // The LDR target the tonemap pass resolves into and the FXAA pass
    // samples. The swapchain cannot be bound as a shader input, so the
    // final anti-aliasing pass reads its tonemapped source from this
    // rgba8 intermediate and writes to the swapchain. No depth: the post
    // chain runs depth-disabled.
    gpu::render_target_descriptor ldr_color_descriptor{};
    ldr_color_descriptor.color = {{gpu::texture_format::rgba8_unorm}};
    ldr_color_descriptor.width = width;
    ldr_color_descriptor.height = height;
    ldr_color_descriptor.with_depth = false;
    m_ldr_color_target = gpu.create_render_target(ldr_color_descriptor);
    m_ldr_color_texture = gpu.render_target_color_texture(m_ldr_color_target);

    m_target_width = width;
    m_target_height = height;
}

void rendering_engine::context::release_color_targets()
{
    auto& gpu = *runtime::current_engine().gpu;

    if (m_ldr_color_target.valid())
    {
        gpu.destroy(m_ldr_color_target);
        m_ldr_color_target = {};
        m_ldr_color_texture = {};
    }
    if (m_scene_color_target.valid())
    {
        gpu.destroy(m_scene_color_target);
        m_scene_color_target = {};
        m_scene_color_texture = {};
    }
    m_target_width = 0;
    m_target_height = 0;
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

rendering_engine::instanced_material& rendering_engine::context::get_instanced_material()
{
    return *m_instanced_material;
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

std::unique_ptr<rendering_engine::grid_material> rendering_engine::context::create_grid_material(float fade_distance)
{
    // The fade distance is a define baked into the template's shaders, so
    // the new instance gets a template of its own, built on the device and
    // against the scene per-frame layout the built-in grid template uses.
    assert(m_grid_material != nullptr && "context::create_grid_material is only valid between init and quit");
    const material_template& builtin = m_grid_material->get_template();
    return std::make_unique<grid_material>(
        grid_material::create_template(builtin.device(), builtin.descriptor().frame_layout, fade_distance));
}

rendering_engine::ui_material& rendering_engine::context::get_ui_material()
{
    return *m_ui_material;
}

rendering_engine::per_draw_ring& rendering_engine::context::get_per_draw_ring()
{
    return *m_per_draw_ring;
}

rendering_engine::tonemap_pass& rendering_engine::context::tonemap()
{
    assert(m_tonemap != nullptr && "context::tonemap is only valid between init and quit");
    return *m_tonemap;
}

void rendering_engine::context::set_post_settings(const post_settings& settings)
{
    m_post_settings = settings;

    // Temporal AA's presence is fixed at init (see context::init): a
    // caller cannot flip it from here, so the stored value always mirrors
    // reality rather than whatever was requested.
    m_post_settings.taa.enabled = (m_taa != nullptr);

    // The tonemap pass already exposes live-tunable exposure / operator
    // setters that rewrite its UBO immediately and only on change; forward
    // to them now rather than waiting for the pass to read frame_context
    // on the next record(), so a caller reading context::tonemap() right
    // after this call sees the new values.
    if (m_tonemap != nullptr)
    {
        m_tonemap->set_exposure(settings.exposure);
        m_tonemap->set_operator(settings.tonemap_op);
    }
}

const rendering_engine::post_settings& rendering_engine::context::get_post_settings() const
{
    return m_post_settings;
}

const rendering_engine::render_stats& rendering_engine::context::get_render_stats() const
{
    return m_render_stats;
}

const rendering_engine::gpu_profiler& rendering_engine::context::get_gpu_profiler() const
{
    return m_gpu_profiler;
}

rendering_engine::gpu::texture rendering_engine::context::scene_color_texture() const
{
    return m_scene_color_texture;
}

rendering_engine::gpu::texture rendering_engine::context::scene_depth_texture() const
{
    return runtime::current_engine().gpu->render_target_depth_texture(m_scene_color_target);
}

rendering_engine::gpu::texture rendering_engine::context::ldr_color_texture() const
{
    return m_ldr_color_texture;
}

rendering_engine::gpu::texture rendering_engine::context::velocity_texture() const
{
    return m_velocity != nullptr ? m_velocity->velocity_texture() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::context::taa_resolve_texture() const
{
    return m_taa != nullptr ? m_taa->output_texture() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::context::directional_shadow_map() const
{
    return m_shadow != nullptr ? m_shadow->shadow_map() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::context::spot_shadow_map() const
{
    return m_spot_shadow != nullptr ? m_spot_shadow->shadow_map() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::context::environment_brdf_lut() const
{
    return m_environment != nullptr ? m_environment->brdf_lut() : gpu::texture{};
}

std::unique_ptr<rendering_engine::standard_material> rendering_engine::context::create_standard_material()
{
    auto material = std::make_unique<standard_material>(m_standard_template);
    if (m_environment != nullptr)
    {
        material->set_environment(*m_environment);
    }
    return material;
}

const std::shared_ptr<rendering_engine::material_template>&
rendering_engine::context::get_standard_material_template() const
{
    return m_standard_template;
}

void rendering_engine::context::set_environment(const environment* env)
{
    m_environment = env;

    // Point the skybox pass at the cube map (or clear it) and mirror the
    // choice onto every live standard material — the built-in one and
    // each instance create_standard_material handed out, whenever it was
    // made — so all their surfaces pick up the matching image-based
    // ambient. Every instance of the standard template is a
    // standard_material: that is the only type constructed over it.
    if (m_skybox != nullptr)
    {
        m_skybox->set_cubemap(env != nullptr ? env->skybox() : gpu::texture{});
    }
    if (m_standard_template == nullptr)
    {
        return;
    }
    // Copy the list: set_environment rebuilds the instance's bind group
    // but never registers or drops an instance, so this is only caution.
    const std::vector<material*> instances = m_standard_template->instances();
    for (material* instance : instances)
    {
        auto* standard = static_cast<standard_material*>(instance);
        if (env != nullptr)
        {
            standard->set_environment(*env);
        }
        else
        {
            standard->clear_environment();
        }
    }
}

void rendering_engine::context::set_fog(const fog_settings& fog)
{
    m_fog = fog;
}
