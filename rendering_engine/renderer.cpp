// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderer.hpp>

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/time.hpp>
#include <platform/window.hpp>
#include <platform/window_settings.hpp>
#include <rendering_engine/camera/camera_registry.hpp>
#include <rendering_engine/camera/perspective_camera.hpp>
#include <rendering_engine/debug_draw/axes_helper.hpp>
#include <rendering_engine/debug_draw/debug_pass.hpp>
#include <rendering_engine/debug_draw/helper.hpp>
#include <rendering_engine/debug_draw/infinite_grid.hpp>
#include <rendering_engine/gpu/command_encoder.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/gpu/shader_hot_reload.hpp>
#include <rendering_engine/gpu/shader_library.hpp>
#include <rendering_engine/graphics_settings.hpp>
#include <rendering_engine/lighting/environment_probe.hpp>
#include <rendering_engine/materials/grid_material.hpp>
#include <rendering_engine/materials/standard_material.hpp>
#include <rendering_engine/passes/depth_prepass.hpp>
#include <rendering_engine/passes/pass.hpp>
#include <rendering_engine/passes/point_shadow_pass.hpp>
#include <rendering_engine/passes/post/auto_exposure_pass.hpp>
#include <rendering_engine/passes/post/bloom_pass.hpp>
#include <rendering_engine/passes/post/fxaa_pass.hpp>
#include <rendering_engine/passes/post/motion_blur_pass.hpp>
#include <rendering_engine/passes/post/taa_pass.hpp>
#include <rendering_engine/passes/post/tonemap_pass.hpp>
#include <rendering_engine/passes/post/velocity_pass.hpp>
#include <rendering_engine/passes/post/volumetric_fog_pass.hpp>
#include <rendering_engine/passes/projection_jitter.hpp>
#include <rendering_engine/passes/scene_pass.hpp>
#include <rendering_engine/passes/shadow_pass.hpp>
#include <rendering_engine/passes/shadow_settings.hpp>
#include <rendering_engine/passes/skybox_pass.hpp>
#include <rendering_engine/passes/spot_shadow_pass.hpp>
#include <rendering_engine/passes/ui_pass.hpp>
#include <rendering_engine/post_process_settings.hpp>
#include <rendering_engine/renderables/renderable.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <rendering_engine/resources/texture_asset.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <exception>

namespace
{
    // The renderer's post settings for the values core::load_settings resolved
    // at startup (settings.json, the ALPHAENGINE_* variables, the command
    // line). The two structs mirror each other field for field; post_settings
    // is the live-tunable surface (renderer::set_post_settings), while
    // post_process_settings is just the resolved startup configuration.
    rendering_engine::post_settings startup_post_settings(const rendering_engine::post_process_settings& source)
    {
        rendering_engine::post_settings settings{};
        settings.exposure = source.exposure;
        switch (source.tonemap)
        {
        case rendering_engine::tonemap_curve::none:
            settings.tonemap_op = rendering_engine::tonemap_operator::none;
            break;
        case rendering_engine::tonemap_curve::reinhard:
            settings.tonemap_op = rendering_engine::tonemap_operator::reinhard;
            break;
        case rendering_engine::tonemap_curve::aces:
            settings.tonemap_op = rendering_engine::tonemap_operator::aces;
            break;
        }

        settings.volumetric.enabled = source.volumetric_fog;
        settings.volumetric.density_scale = source.volumetric_fog_density_scale;
        settings.volumetric.anisotropy = source.volumetric_fog_anisotropy;
        settings.volumetric.max_distance = source.volumetric_fog_max_distance;
        settings.volumetric.steps = static_cast<int>(source.volumetric_fog_steps);
        settings.volumetric.intensity = source.volumetric_fog_intensity;

        settings.motion_blur.enabled = source.motion_blur;
        settings.motion_blur.intensity = source.motion_blur_intensity;
        settings.motion_blur.samples = static_cast<int>(source.motion_blur_samples);
        settings.motion_blur.max_radius = source.motion_blur_max_radius;

        settings.bloom.enabled = source.bloom;
        settings.bloom.threshold = source.bloom_threshold;
        settings.bloom.knee = source.bloom_knee;
        settings.bloom.strength = source.bloom_strength;

        settings.auto_exposure.enabled = source.auto_exposure;
        settings.auto_exposure.min_ev = source.auto_exposure_min_ev;
        settings.auto_exposure.max_ev = source.auto_exposure_max_ev;
        settings.auto_exposure.speed_up = source.auto_exposure_speed_up;
        settings.auto_exposure.speed_down = source.auto_exposure_speed_down;
        settings.auto_exposure.compensation = source.auto_exposure_compensation;

        settings.grading.lut = source.grading_lut;
        settings.grading.intensity = source.grading_intensity;

        // taa.enabled is overwritten by set_post_settings with the pass's
        // real presence (graphics.temporal_aa decides that); only the
        // feedback is a post setting.
        settings.taa.feedback = source.taa_feedback;
        settings.fxaa.enabled = source.fxaa;
        return settings;
    }
} // namespace

rendering_engine::renderer::renderer() = default;
rendering_engine::renderer::~renderer() = default;

void rendering_engine::renderer::init(const render_services& services)
{
    LOG_INF("Init Rendering Engine");

    assert(services.device != nullptr && services.window != nullptr && services.events != nullptr &&
           "renderer::init: the device, the window and the event bus are required");
    m_services = services;
    gpu::device& device = *services.device;

#if _DEBUG
    // Debug builds watch the directory the shader library reads its
    // overrides from (the source tree's shaders/, or
    // ALPHAENGINE_SHADER_DIR) and swap an edited shader into every
    // pipeline built from it. Installed before the passes and templates
    // below create their modules, so each registers with it.
    if (const std::filesystem::path& root = gpu::shader_library::override_root(); !root.empty())
    {
        m_shader_hot_reload = std::make_unique<gpu::shader_hot_reload>(device, root);
    }
#endif

    // Tell the device about the initial backbuffer dimensions so that
    // begin_render_pass can default the viewport to the full window. The
    // drawable is measured in pixels rather than taken from the settings'
    // logical size: on a high-density display the two differ by the
    // display scale, and the swapchain and render targets follow pixels.
    const platform::window_extent drawable = services.window->pixel_size();
    const uint32_t width = drawable.width;
    const uint32_t height = drawable.height;
    device.resize_swapchain(width, height);

    // Report the drawable's aspect to the world's cameras: every attached
    // camera takes it now and any camera attached later takes it on
    // attach, so the projection always matches the drawable. The settings'
    // logical size stands in while the window has no drawable.
    const float settings_aspect = services.window_settings != nullptr ? services.window_settings->aspect_ratio() : 1.0f;
    m_world.set_drawable_aspect(drawable_aspect_ratio(width, height, settings_aspect));

    // Keep the swapchain extent, the off-screen targets and the passes in
    // step with the drawable as the window is resized, maximised, restored
    // or moved across displays. The listener runs from the window's event
    // pump in engine::tick, before the frame is built, so on_resize never
    // recreates a target a command buffer is being recorded against.
    m_window_resized_subscription = services.events->subscribe<core::window_resized>(
        [this](const core::window_resized& e)
        {
            m_services.device->resize_swapchain(e.m_pixel_width, e.m_pixel_height);
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
    const graphics_settings graphics = services.graphics != nullptr ? *services.graphics : graphics_settings{};
    const bool taa_enabled = graphics.temporal_aa && width != 0 && height != 0;

    // Construct the built-in passes first — each pass owns the
    // per-frame bind-group layout its matching material reads at
    // pipeline-create time. The shadow passes walk the world's
    // scene-renderable registry, like the scene pass, and size their maps
    // and biases from the shadow settings, fixed at startup. They run
    // ahead of the scene pass, which reads their maps and each frame's
    // fitted matrices through the frame context (render() publishes the
    // passes there), so no pass is handed another at construction.
    const shadow_settings shadow_config = services.shadows != nullptr ? *services.shadows : shadow_settings{};
    auto shadow = std::make_unique<shadow_pass>(device, &m_world.scene_renderables(), shadow_config);
    m_shadow = shadow.get();
    // The omni shadow pass renders six depth faces from the first shadow-casting
    // point light; like the directional shadow it runs before the scene pass so
    // its maps are ready for the per-frame bind group.
    auto point_shadow = std::make_unique<point_shadow_pass>(device, &m_world.scene_renderables(), shadow_config);
    m_point_shadow = point_shadow.get();
    // The spot shadow pass renders a single perspective depth map from the
    // first shadow-casting spot light; also runs before the scene pass.
    auto spot_shadow = std::make_unique<spot_shadow_pass>(device, &m_world.scene_renderables(), shadow_config);
    m_spot_shadow = spot_shadow.get();
    // Above the parallel draw threshold the scene pass records its draws
    // from the job pool's workers (Vulkan only); 0 keeps it serial.
    const uint32_t parallel_draw_threshold = graphics.parallel_draw_threshold;
    auto scene = std::make_unique<scene_pass>(
        device, services.jobs, &m_world.scene_renderables(), &m_render_stats, taa_enabled, parallel_draw_threshold);
    m_scene = scene.get();
    // The optional depth pre-pass runs right before the scene pass, over
    // the scene pass's own draw list and per-frame group (reached through
    // frame_context::scene), and lays the opaque depth into the scene
    // target for it to load. It is always in the pass list and records
    // nothing while disabled, so set_depth_prepass can flip it at runtime.
    auto depth_pre = std::make_unique<depth_prepass>(device);
    m_depth_prepass_enabled = graphics.depth_prepass;
    // The material library below is built against the same per-frame
    // layout the scene pass binds at slot 0.
    const gpu::bind_group_layout scene_frame_layout = scene->frame_bind_group_layout();
    // The skybox pass runs after the scene pass and composites the cube-map
    // background into the HDR target where no geometry was drawn. It stays
    // dormant until set_environment supplies a cube map.
    auto skybox = std::make_unique<skybox_pass>(device);
    m_skybox = skybox.get();
    // Volumetric fog marches the height-fog medium toward the finalised
    // scene depth and blends its lit haze and light shafts over the HDR
    // target, ahead of bloom and tonemap so they treat it like the rest of
    // the scene. It binds the scene pass's jittered per-frame group (the
    // view the depth was rasterised with, the lights, the shadow maps),
    // read through frame_context::scene, and draws nothing until
    // post_settings::volumetric enables it.
    auto volumetric_fog = std::make_unique<volumetric_fog_pass>(device, scene_frame_layout, width, height);
    // Per-pixel motion vectors are reconstructed from the scene depth
    // buffer: the velocity pass samples the HDR target's depth attachment
    // through frame_context::scene_depth_texture each frame. They drive
    // the TAA history reprojection and motion blur; the pass is always
    // built, since motion blur can be switched on at runtime, and draws
    // only while one of the two consumes it.
    auto velocity = std::make_unique<velocity_pass>(device, width, height);
    m_velocity = velocity.get();
    // Motion blur smears the HDR image along those vectors, after the
    // volumetric fog (so the haze smears with the scene) and before bloom
    // and auto exposure (so the glow spreads from, and the exposure
    // meters, the blurred image). It writes a target of its own; render()
    // publishes it as frame_context::hdr_color_target / _texture while
    // the pass draws, and the scene colour otherwise.
    auto motion_blur = std::make_unique<motion_blur_pass>(device, width, height);
    m_motion_blur = motion_blur.get();
    // Bloom runs between the scene and tonemap passes: it reads the HDR
    // image, blurs the bright pixels and additively composites the glow
    // back into the same target, so tonemap maps the bloomed result.
    // Neither pass takes the HDR texture here: both read it from
    // frame_context::hdr_color_texture every frame and rebind when the
    // handle changes, so a resize that recreates the target (or motion
    // blur switching on) reaches them without re-plumbing.
    auto bloom = std::make_unique<bloom_pass>(device, width, height);
    // Eye adaptation meters the bloomed HDR image tonemap is about to map
    // and leaves the adapted exposure in a 1x1 texture tonemap samples
    // (frame_context::exposure_texture) while it is enabled.
    auto auto_exposure = std::make_unique<auto_exposure_pass>(device);
    m_auto_exposure = auto_exposure.get();
    auto post = std::make_unique<tonemap_pass>(device);
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
    std::unique_ptr<taa_pass> taa;
    if (taa_enabled)
    {
        taa = std::make_unique<taa_pass>(device, width, height);
        m_taa = taa.get();
    }
    // Start the post chain from the persisted values core::load_settings
    // resolved (settings.json, ALPHAENGINE_* variables, command line).
    // set_post_settings forwards exposure / operator to the tonemap pass
    // just built and overwrites post_settings::taa.enabled with the TAA
    // pass's real presence: temporal AA is only ever decided here, at
    // init, from graphics.temporal_aa.
    set_post_settings(startup_post_settings(services.post != nullptr ? *services.post : post_process_settings{}));
    // FXAA closes the post chain: it samples the TAA resolve when one is
    // published (else the LDR target) and writes the anti-aliased image to
    // the swapchain. It declares whichever of the two it will actually
    // read so the pass list validation checks the real wiring.
    auto fxaa = std::make_unique<fxaa_pass>(device, width, height, taa_enabled);
    // The UI pass owns the pixel-space projection the ui template reads at
    // slot 0; it follows the drawable through pass::resize.
    auto ui = std::make_unique<ui_pass>(device, &m_world.ui_renderables(), width, height);
    const gpu::bind_group_layout ui_frame_layout = ui->frame_bind_group_layout();
#if _DEBUG
    // The debug pass binds the scene pass's per-frame camera group at
    // slot 0 so the line-based debug gizmos project with the same camera.
    // It uses the unjittered overlay group: the debug pass paints after the
    // TAA resolve, so the projection jitter would otherwise show up as a
    // sub-pixel wobble on the gizmos rather than being averaged away.
    auto debug = std::make_unique<debug_draw::debug_pass>(&m_world.debug_renderables());
    m_debug = debug.get();
#endif

    // Build the material library: one template per built-in type against
    // the per-frame layouts the passes above expose (the 3D templates
    // reserve slot 0 for the scene_pass's per-frame group, the ui template
    // for the ui_pass's), and the built-in instance of each.
    m_materials.init(device, scene_frame_layout, ui_frame_layout);

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
    // that target with the environment cube map, the velocity pass
    // reconstructs per-pixel motion vectors from the finalised depth for
    // the TAA reprojection and motion blur, the volumetric fog pass blends
    // the lit medium over it, the motion blur pass smears the result along
    // the motion vectors, the bloom post pass blurs its bright pixels back
    // into that image, the auto-exposure pass meters it, the tonemap post
    // pass maps it to LDR in the off-screen LDR target (exposed, graded),
    // the optional TAA post pass accumulates the jittered LDR frames into
    // a supersampled image, the FXAA post pass anti-aliases that result
    // onto the swapchain, and the UI pass composites on top. The
    // debug pass is appended in debug builds only so debug visuals read on
    // top of the game UI; release builds drop it entirely so the
    // overlay registry has no consumer and the stage costs nothing.
    // Further post effects insert between scene and ui by pushing into
    // this list; future debug consumers (wireframe, gizmos, frustum
    // visualisations) register with the debug-renderable registry
    // rather than adding new passes.
    // The shadow pass renders the light's depth map first so the scene
    // pass can sample it the same frame. The depth pre-pass follows the
    // shadow passes (the scene pass's per-frame uploads, which the
    // pre-pass triggers, read their matrices) and precedes the scene pass
    // that loads its depth.
    m_passes.add(std::move(shadow));
    m_passes.add(std::move(point_shadow));
    m_passes.add(std::move(spot_shadow));
    m_passes.add(std::move(depth_pre));
    m_passes.add(std::move(scene));
    m_passes.add(std::move(skybox));
    // Motion vectors are computed from the finalised scene depth, before
    // the post chain consumes the colour, so the velocity pass sits right
    // after the geometry and skybox.
    m_passes.add(std::move(velocity));
    m_passes.add(std::move(volumetric_fog));
    m_passes.add(std::move(motion_blur));
    m_passes.add(std::move(bloom));
    m_passes.add(std::move(auto_exposure));
    m_passes.add(std::move(post));
    if (taa)
    {
        m_passes.add(std::move(taa));
    }
    m_passes.add(std::move(fxaa));
    m_passes.add(std::move(ui));
#if _DEBUG
    m_passes.add(std::move(debug));
#endif

    // Validate the now-final pass list. The swapchain image and, with
    // temporal AA on, the TAA history are valid at frame start without an
    // in-frame producer, so import them as external; every other resource
    // is produced by a pass. Each pass declares its reads/writes (those
    // that override declare_io) and the list checks the ordering; it
    // records in the order the passes were added either way.
    m_passes.import_external("swapchain");
    if (taa_enabled)
    {
        m_passes.import_external("taa_history");
    }
    // A hazard is a pass reading a resource nothing before it produced: a
    // mis-ordered or mis-declared pass list, i.e. a programming error. It
    // stops a debug build here; a release build logs it (the list already
    // reported each offending read) and renders in the declared order.
    const bool hazard_free = m_passes.validate();
    assert(hazard_free && "pass list: a pass reads a resource before any pass produces it");
    if (!hazard_free)
    {
        LOG_ERR("Rendering Engine: the pass list has hazards; it is mis-declared or mis-ordered (see the pass_list "
                "errors above)");
    }

    // Per-pass GPU timings over the pass list; disabled on a device
    // without timestamp queries.
    m_gpu_profiler.init(device, m_passes.pass_names());

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
    m_debug_helpers.push_back(std::make_unique<debug_draw::infinite_grid>());
    m_debug_helpers.push_back(std::make_unique<debug_draw::axes_helper>());
#endif
}

void rendering_engine::renderer::quit()
{
    // The teardown walks the members from the last declared to the first
    // (see renderer.hpp); the engine takes the device and the window down
    // after it.

    // Stop tracking window resizes before the device the listener
    // resizes goes away.
    m_window_resized_subscription.reset();

#if _DEBUG
    // Nothing reloads during teardown; the modules it tracks go with
    // their owners below.
    m_shader_hot_reload.reset();
#endif

    // Release the built-in debug helpers before the line material and the
    // GPU device they reference; their destructors unregister from the
    // world's registries and free their line buffers. Empty in release.
    // Game-owned helpers must likewise be released before quit.
    m_debug_helpers.clear();

    // The profiler's query sets go before the device does. Without a
    // device init never ran, and there is nothing to release.
    if (m_services.device != nullptr)
    {
        m_gpu_profiler.shutdown(*m_services.device);
    }

    // Drop the passes first; their record() bodies reach for the
    // event bus we're about to release, and the passes own per-frame
    // bind-group layouts referenced by the materials' pipelines.
    m_passes.clear();
    m_skybox = nullptr;
    m_tonemap = nullptr;
    m_velocity = nullptr;
    m_motion_blur = nullptr;
    m_auto_exposure = nullptr;
    m_taa = nullptr;
    m_scene = nullptr;
    m_shadow = nullptr;
    m_point_shadow = nullptr;
    m_spot_shadow = nullptr;
    m_debug = nullptr;
    m_prev_camera = nullptr;
    m_has_prev_view_projection = false;

    // The grading LUT is a cached asset whose texture this handle keeps
    // alive; drop it while the device it is freed through is still up.
    m_grading_lut.reset();
    m_grading_lut_path.clear();

    // Then the material library, whose templates own pipelines that
    // reference the device. Release them before the device tears its
    // pools down.
    m_materials.quit();

    // Release the off-screen HDR and LDR targets before the device tears
    // its pools down. The colour and depth attachments are owned by the
    // targets so destroy() releases them too.
    release_color_targets();

    // Last, the world: withdraw the drawable aspect it hands to attaching
    // cameras, since there is no drawable to match now. Every renderable,
    // pass and helper that pointed into its registries is gone.
    m_world.quit();

    // Forget the subsystems init was handed; the owner takes them down
    // next.
    m_services = render_services{};

    LOG_INF("Quit Rendering Engine");
}

void rendering_engine::renderer::render()
{
    auto& gpu = *m_services.device;

#if _DEBUG
    // Pick up shader edits between frames: a rebuilt pipeline replaces
    // the old one behind its handle before this frame binds anything,
    // and the old objects are released once the frames using them have
    // retired. Rescans at most about once a second.
    if (m_shader_hot_reload != nullptr)
    {
        m_shader_hot_reload->poll();
    }
#endif

    // Resolve a changed colour-grading LUT path through the asset cache
    // first, outside the frame like any other asset load (the upload is
    // ordered ahead of the frame that samples it). A no-op while the path
    // is unchanged. Motion blur likewise allocates its full-resolution
    // target here, the first time it is switched on.
    update_grading_lut();
    if (m_motion_blur != nullptr)
    {
        m_motion_blur->ensure_target(m_post_settings.motion_blur);
    }

    // Open the device frame before anything below touches GPU-visible
    // memory. The device blocks here until the frame that last recorded
    // into this frame's slot has finished and then frees the resources
    // whose destruction it deferred while a command buffer could still
    // reference them; the per-frame UBO writes the passes make during the
    // walk land in this slot's copy of each buffer (see
    // buffer_usage_hint::dynamic_data), so they never race a frame still
    // in flight.
    gpu.begin_frame();
    m_in_frame = true;

    // Standard materials sampling shared texture assets rebuild their
    // bind group when an asset's texture was replaced since (an
    // asynchronous load resolved in asset_cache::pump, or a debug hot
    // reload swapped it). Here, with the frame open and no pass recording
    // yet, the old group is released safely and every pass binds the new
    // one.
    m_materials.refresh_texture_assets();

    // The previous frame's work has retired (or its queries are polled
    // without waiting), so its per-pass timestamps can be read now.
    m_gpu_profiler.resolve(gpu);

    // Capture per-frame state once so passes cannot disagree about
    // which camera or backbuffer is active mid-frame, and so they
    // do not have to re-run the camera arbitration on every entry.
    // The world's active_camera() is the arbitration's pick for this
    // frame: the highest-priority attached, enabled camera.
    frame_context ctx{};
    ctx.swapchain_target = gpu.swapchain_target();
    ctx.active_camera = m_world.active_camera();
    ctx.viewport_width = m_target_width;
    ctx.viewport_height = m_target_height;
    ctx.frame_index = m_frame_index;
    // The engine clock ticked at the top of this frame; core::time reports
    // milliseconds, the shaders see seconds.
    if (m_services.time != nullptr)
    {
        ctx.time_seconds = m_services.time->total_time() / 1000.0f;
        ctx.delta_seconds = static_cast<float>(m_services.time->delta_time() / 1000.0);
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
    // The textures the velocity and temporal-AA passes own are published
    // the same way: read from the owning pass every frame so the TAA
    // resolve, motion blur and FXAA rebind after a resize recreated them.
    // The TAA resolve is invalid while TAA is off.
    ctx.velocity_texture = (m_velocity != nullptr) ? m_velocity->velocity_texture() : gpu::texture{};
    ctx.taa_resolve_texture = (m_taa != nullptr) ? m_taa->output_texture() : gpu::texture{};
    ctx.fog = m_world.fog();
    ctx.depth_prepass = m_depth_prepass_enabled;
    ctx.post = m_post_settings;
    // The passes whose output later passes consume, so none of them holds
    // another: the scene pass reads the shadow passes, the depth pre-pass,
    // the volumetric fog and the debug pass read the scene pass.
    ctx.scene = m_scene;
    ctx.directional_shadow = m_shadow;
    ctx.point_shadow = m_point_shadow;
    ctx.spot_shadow = m_spot_shadow;
    // The HDR image the chain after motion blur works on: the blurred copy
    // when the pass draws this frame, else the scene colour itself. Asked
    // of the pass with the same frame context its record() will see, so
    // producer and consumers never disagree.
    const bool motion_blur = (m_motion_blur != nullptr) && m_motion_blur->draws(ctx);
    ctx.hdr_color_target = motion_blur ? m_motion_blur->output_target() : m_scene_color_target;
    ctx.hdr_color_texture = motion_blur ? m_motion_blur->output_texture() : m_scene_color_texture;
    // Tonemap takes its exposure from the eye-adaptation result only
    // while the pass leaves a valid one this frame, and grades only with
    // a usable table and a visible blend; otherwise it draws the variant
    // without that effect.
    ctx.exposure_texture = (m_auto_exposure != nullptr && m_auto_exposure->produces_exposure(ctx))
                               ? m_auto_exposure->exposure_texture()
                               : gpu::texture{};
    ctx.grading_lut_texture = (m_grading_lut != nullptr && m_post_settings.grading.intensity > 0.0f)
                                  ? m_grading_lut->texture
                                  : gpu::texture{};

    // Every pass prepares first, in list order: the per-frame uploads,
    // the culling and sorting, the bind-group rebuilds and every
    // cross-pass read (the shadow fits the scene pass uploads, the
    // pre-pass's announcement to the scene pass) happen here, on this
    // thread, before anything is recorded — so the record walk below
    // only encodes from finished state and the scene pass may hand its
    // chunks to the job pool's workers.
    m_passes.prepare(ctx);

    // One encoder records the pass list in order — each pass in a debug
    // group and between the profiler's timestamps — then submits.
    auto encoder = gpu.create_command_encoder();
    m_gpu_profiler.begin_frame(*encoder);
    m_passes.record(*encoder, ctx, &m_gpu_profiler);
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

    // Close the frame: the device presents the swapchain image it
    // acquired for this frame.
    gpu.end_frame();
    m_in_frame = false;
}

void rendering_engine::renderer::on_resize(uint32_t pixel_width, uint32_t pixel_height)
{
    // The listener that calls this runs from the window's event pump,
    // never from inside render(): the targets released below may still be
    // bound to the frame being recorded otherwise.
    assert(!m_in_frame && "renderer::on_resize must not run while a frame is being recorded");

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

    auto& gpu = *m_services.device;

    // Recreate the renderer-owned targets: new ones first, so every
    // consumer that compares the handle it bound against the one
    // frame_context publishes (tonemap, bloom, the velocity pass's depth,
    // the TAA resolve's LDR input) sees a different handle next frame;
    // then release the old ones. The device defers the free until the
    // last command buffer that referenced them has retired.
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
    m_passes.resize(pixel_width, pixel_height);

    // The projection follows the drawable so the image is not stretched:
    // the world forwards the aspect to every attached camera and hands it
    // to any camera attached later. Both dimensions are non-zero here, so
    // the fallback is never used.
    m_world.set_drawable_aspect(drawable_aspect_ratio(pixel_width, pixel_height, 1.0f));

    LOG_INF("Rendering Engine: render targets resized to %ux%u", pixel_width, pixel_height);
}

void rendering_engine::renderer::create_color_targets(uint32_t width, uint32_t height)
{
    auto& gpu = *m_services.device;

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

void rendering_engine::renderer::release_color_targets()
{
    // A valid target was created on the device init was handed, so the
    // device is only reached when there is one to release.
    if (m_ldr_color_target.valid())
    {
        m_services.device->destroy(m_ldr_color_target);
        m_ldr_color_target = {};
        m_ldr_color_texture = {};
    }
    if (m_scene_color_target.valid())
    {
        m_services.device->destroy(m_scene_color_target);
        m_scene_color_target = {};
        m_scene_color_texture = {};
    }
    m_target_width = 0;
    m_target_height = 0;
}

void rendering_engine::renderer::register_scene_renderable(renderable* r)
{
    m_world.register_scene_renderable(r);
}

void rendering_engine::renderer::unregister_scene_renderable(renderable* r)
{
    m_world.unregister_scene_renderable(r);
}

void rendering_engine::renderer::register_ui_renderable(renderable* r)
{
    m_world.register_ui_renderable(r);
}

void rendering_engine::renderer::unregister_ui_renderable(renderable* r)
{
    m_world.unregister_ui_renderable(r);
}

void rendering_engine::renderer::register_debug_renderable(renderable* r)
{
    m_world.register_debug_renderable(r);
}

void rendering_engine::renderer::unregister_debug_renderable(renderable* r)
{
    m_world.unregister_debug_renderable(r);
}

void rendering_engine::renderer::set_overlay(gpu::overlay_renderer* overlay)
{
    if (m_debug != nullptr)
    {
        m_debug->set_overlay(overlay);
    }
}

rendering_engine::basic_material& rendering_engine::renderer::get_basic_material()
{
    return m_materials.get_basic_material();
}

rendering_engine::instanced_material& rendering_engine::renderer::get_instanced_material()
{
    return m_materials.get_instanced_material();
}

rendering_engine::phong_material& rendering_engine::renderer::get_phong_material()
{
    return m_materials.get_phong_material();
}

rendering_engine::standard_material& rendering_engine::renderer::get_standard_material()
{
    return m_materials.get_standard_material();
}

rendering_engine::points_material& rendering_engine::renderer::get_points_material()
{
    return m_materials.get_points_material();
}

rendering_engine::line_material& rendering_engine::renderer::get_line_material()
{
    return m_materials.get_line_material();
}

rendering_engine::line_material& rendering_engine::renderer::get_debug_line_material()
{
    return m_materials.get_debug_line_material();
}

rendering_engine::grid_material& rendering_engine::renderer::get_grid_material()
{
    return m_materials.get_grid_material();
}

std::unique_ptr<rendering_engine::grid_material> rendering_engine::renderer::create_grid_material(float fade_distance)
{
    return m_materials.create_grid_material(fade_distance);
}

rendering_engine::ui_material& rendering_engine::renderer::get_ui_material()
{
    return m_materials.get_ui_material();
}

rendering_engine::tonemap_pass& rendering_engine::renderer::tonemap()
{
    assert(m_tonemap != nullptr && "renderer::tonemap is only valid between init and quit");
    return *m_tonemap;
}

void rendering_engine::renderer::set_post_settings(const post_settings& settings)
{
    m_post_settings = settings;

    // Temporal AA's presence is fixed at init (see renderer::init): a
    // caller cannot flip it from here, so the stored value always mirrors
    // reality rather than whatever was requested.
    m_post_settings.taa.enabled = (m_taa != nullptr);

    // The tonemap pass already exposes live-tunable exposure / operator
    // setters that rewrite its UBO immediately and only on change; forward
    // to them now rather than waiting for the pass to read frame_context
    // on the next record(), so a caller reading renderer::tonemap() right
    // after this call sees the new values.
    if (m_tonemap != nullptr)
    {
        m_tonemap->set_exposure(settings.exposure);
        m_tonemap->set_operator(settings.tonemap_op);
    }
}

const rendering_engine::post_settings& rendering_engine::renderer::get_post_settings() const
{
    return m_post_settings;
}

bool rendering_engine::renderer::grading_lut_loaded() const
{
    return m_grading_lut != nullptr && m_grading_lut_path == m_post_settings.grading.lut;
}

void rendering_engine::renderer::update_grading_lut()
{
    const std::string& path = m_post_settings.grading.lut;
    if (path == m_grading_lut_path)
    {
        return;
    }
    // Remember the path before trying it, so a table that fails is
    // reported once rather than every frame until the path changes.
    m_grading_lut_path = path;
    m_grading_lut.reset();
    if (path.empty())
    {
        LOG_INF("Colour grading: off");
        return;
    }

    if (m_services.assets == nullptr)
    {
        LOG_WRN("Colour grading: no asset cache to load '%s' through; grading stays off", path.c_str());
        return;
    }

    // Linear, not sRGB: the texels are already the encoded output colours
    // the lookup must return unchanged.
    std::shared_ptr<texture_asset> lut;
    try
    {
        lut = m_services.assets->load_texture(core::os::utf8_path(path), assets::color_space::linear);
    }
    catch (const std::exception& error)
    {
        LOG_WRN("Colour grading: could not load LUT '%s' (%s); grading stays off", path.c_str(), error.what());
        return;
    }

    // An N^2 x N strip: N slices of N x N side by side. Anything else would
    // be looked up with the wrong slice size.
    const uint64_t size = lut->height;
    if (size < 2 || static_cast<uint64_t>(lut->width) != size * size)
    {
        LOG_WRN("Colour grading: '%s' is %ux%u, not an N*N x N strip LUT; grading stays off",
                path.c_str(),
                lut->width,
                lut->height);
        return;
    }
    m_grading_lut = std::move(lut);
    LOG_INF("Colour grading: using '%s' (%ux%ux%u)",
            path.c_str(),
            static_cast<unsigned int>(size),
            static_cast<unsigned int>(size),
            static_cast<unsigned int>(size));
}

void rendering_engine::renderer::set_depth_prepass(bool enabled)
{
    if (enabled != m_depth_prepass_enabled)
    {
        LOG_INF("Rendering Engine: depth pre-pass %s", enabled ? "enabled" : "disabled");
    }
    m_depth_prepass_enabled = enabled;
}

bool rendering_engine::renderer::depth_prepass_enabled() const
{
    return m_depth_prepass_enabled;
}

const rendering_engine::render_stats& rendering_engine::renderer::get_render_stats() const
{
    return m_render_stats;
}

const rendering_engine::gpu_profiler& rendering_engine::renderer::get_gpu_profiler() const
{
    return m_gpu_profiler;
}

rendering_engine::gpu::texture rendering_engine::renderer::scene_color_texture() const
{
    return m_scene_color_texture;
}

rendering_engine::gpu::texture rendering_engine::renderer::scene_depth_texture() const
{
    return m_services.device != nullptr ? m_services.device->render_target_depth_texture(m_scene_color_target)
                                        : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::ldr_color_texture() const
{
    return m_ldr_color_texture;
}

rendering_engine::gpu::texture rendering_engine::renderer::velocity_texture() const
{
    return m_velocity != nullptr ? m_velocity->velocity_texture() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::taa_resolve_texture() const
{
    return m_taa != nullptr ? m_taa->output_texture() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::directional_shadow_map() const
{
    return m_shadow != nullptr ? m_shadow->shadow_map() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::spot_shadow_map() const
{
    return m_spot_shadow != nullptr ? m_spot_shadow->shadow_map() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::environment_brdf_lut() const
{
    return m_world.environment_brdf_lut();
}

std::unique_ptr<rendering_engine::standard_material> rendering_engine::renderer::create_standard_material()
{
    return m_materials.create_standard_material(m_world.environment());
}

const std::shared_ptr<rendering_engine::material_template>&
rendering_engine::renderer::get_standard_material_template() const
{
    return m_materials.get_standard_material_template();
}

void rendering_engine::renderer::set_environment(const environment_probe* env)
{
    m_world.set_environment(env);

    // Point the skybox pass at the cube map (or clear it) and mirror the
    // choice onto every live standard material, so all their surfaces
    // pick up the matching image-based ambient.
    if (m_skybox != nullptr)
    {
        m_skybox->set_cubemap(env != nullptr ? env->skybox() : gpu::texture{});
    }
    m_materials.set_environment(env);
}

void rendering_engine::renderer::set_fog(const fog_settings& fog)
{
    m_world.set_fog(fog);
}
