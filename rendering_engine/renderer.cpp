// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <rendering_engine/renderer.hpp>

#include <core/event_engine.hpp>
#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/time.hpp>
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
#include <rendering_engine/passes/frame_resources.hpp>
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
#include <rendering_engine/resources/asset_cache.hpp>
#include <rendering_engine/resources/texture_asset.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>

namespace
{
    // Brackets the frame the renderer records on its world (see
    // render_world::begin_frame).
    class world_frame_scope
    {
    public:
        explicit world_frame_scope(rendering_engine::render_world& world) : m_world{world}
        {
            m_world.begin_frame();
        }

        world_frame_scope(const world_frame_scope&) = delete;
        world_frame_scope& operator=(const world_frame_scope&) = delete;

        ~world_frame_scope()
        {
            m_world.end_frame();
        }

    private:
        rendering_engine::render_world& m_world;
    };

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

    assert(services.device != nullptr && services.events != nullptr &&
           "renderer::init: the device and the event bus are required");
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
    const uint32_t width = services.drawable_width;
    const uint32_t height = services.drawable_height;
    device.resize_swapchain(width, height);

    // Report the drawable's aspect to the world, whose camera owners hand
    // it to their cameras — the ones already there and any added later —
    // so the projection always matches the drawable. The fallback aspect
    // stands in while the drawable is empty.
    m_world.set_drawable_aspect(drawable_aspect_ratio(width, height, services.fallback_aspect));

    // Keep the swapchain extent and the size the swapchain views follow in
    // step with the drawable as the window is resized, maximised, restored
    // or moved across displays. The listener runs from the window's event
    // pump in engine::tick, before the frame is built, so on_resize never
    // changes the size of a frame being recorded. Each view builds its
    // targets the first frame it renders.
    m_drawable_width = width;
    m_drawable_height = height;
    m_window_resized_subscription = services.events->subscribe<core::window_resized>(
        [this](const core::window_resized& e)
        {
            m_services.device->resize_swapchain(e.m_pixel_width, e.m_pixel_height);
            on_resize(e.m_pixel_width, e.m_pixel_height);
        });

    // Temporal AA is decided once, up front: it decides whether the TAA
    // pass is registered, which gates the projection jitter the scene pass
    // applies (and the unjittered overlay group it builds for the debug
    // pass) and the velocity pass. Off when the setting is off or the
    // drawable is degenerate, in which case the LDR target flows straight
    // into FXAA.
    const graphics_settings graphics = services.graphics != nullptr ? *services.graphics : graphics_settings{};
    const bool taa_enabled = graphics.temporal_aa && width != 0 && height != 0;

    // Construct the built-in passes first — each pass owns the
    // per-frame bind-group layout its matching material reads at
    // pipeline-create time. No pass is handed another: everything one
    // hands the next (the shadow maps and fits, the scene pass's view and
    // draw list, the motion vectors, the TAA resolve, the eye adaptation,
    // the HDR image motion blur replaces) travels through the view's
    // resource store (see passes/frame_resources.hpp), the targets they
    // draw into are the view's, published there every frame, and what a
    // pass keeps at a view's size or across a view's frames it keeps in
    // the view's resource set (passes/view_resources.hpp).
    //
    // The shadow passes cull the frame's mesh draws, like the scene pass,
    // and size their maps and biases from the shadow settings, fixed at
    // startup: the directional cascades, the first shadow-casting point
    // light's cube and the first shadow-casting spot light's map, which
    // the scene pass samples the same frame.
    const shadow_settings shadow_config = services.shadows != nullptr ? *services.shadows : shadow_settings{};
    auto shadow = std::make_unique<shadow_pass>(device, shadow_config);
    auto point_shadow = std::make_unique<point_shadow_pass>(device, shadow_config);
    auto spot_shadow = std::make_unique<spot_shadow_pass>(device, shadow_config);
    // Above the parallel draw threshold the scene pass records its draws
    // from the job pool's workers (Vulkan only); 0 keeps it serial.
    const uint32_t parallel_draw_threshold = graphics.parallel_draw_threshold;
    auto scene =
        std::make_unique<scene_pass>(device, services.jobs, &m_render_stats, taa_enabled, parallel_draw_threshold);
    // The optional depth pre-pass lays the opaque depth down over the
    // scene pass's own draw list and per-frame group. It is always
    // registered and records nothing while disabled, so set_depth_prepass
    // can flip it at runtime.
    auto depth_pre = std::make_unique<depth_prepass>(device);
    m_depth_prepass_enabled = graphics.depth_prepass;
    // The material library below is built against the same per-frame
    // layout the scene pass binds at slot 0, and so is the volumetric
    // fog's march.
    const gpu::bind_group_layout scene_frame_layout = scene->frame_bind_group_layout();
    // The skybox composites the environment cube map into the HDR target
    // where no geometry was drawn; dormant while the world has none.
    auto skybox = std::make_unique<skybox_pass>(device);
    // Volumetric fog marches the height-fog medium toward the finalised
    // scene depth and blends its lit haze and light shafts over the HDR
    // target, ahead of bloom and tonemap so they treat it like the rest of
    // the scene; it draws nothing until post_settings::volumetric enables
    // it.
    auto volumetric_fog = std::make_unique<volumetric_fog_pass>(device, scene_frame_layout);
    // Per-pixel motion vectors, reconstructed from the scene depth, drive
    // the TAA history reprojection and motion blur; the pass is always
    // built, since motion blur can be switched on at runtime, and draws
    // only while one of the two consumes it.
    auto velocity = std::make_unique<velocity_pass>(device);
    // Motion blur smears the HDR image along those vectors into a target
    // of its own, which it hands the passes after it in place of the scene
    // colour; bloom blurs the bright pixels of that image back into it,
    // auto exposure meters it and tonemap maps it (exposed, graded) to the
    // LDR target.
    auto motion_blur = std::make_unique<motion_blur_pass>(device);
    auto bloom = std::make_unique<bloom_pass>(device);
    auto auto_exposure = std::make_unique<auto_exposure_pass>(device);
    auto tonemap = std::make_unique<tonemap_pass>(device);
    // Temporal AA optionally slots in between tonemap and FXAA: it
    // accumulates the projection-jittered frames the scene pass produces
    // (Halton sub-pixel offsets from frame_context::jitter, published only
    // while this is registered) into a stable, supersampled LDR image,
    // then FXAA cleans up whatever spatial edges remain.
    std::unique_ptr<taa_pass> taa;
    if (taa_enabled)
    {
        taa = std::make_unique<taa_pass>(device);
    }
    // FXAA closes the post chain: it samples the TAA resolve while one is
    // published (else the LDR target) and writes the anti-aliased image to
    // the swapchain.
    auto fxaa = std::make_unique<fxaa_pass>(device);
    // The UI pass owns the layout of the pixel-space projection the ui
    // template reads at slot 0; each view gets the projection at its size.
    auto ui = std::make_unique<ui_pass>(device);
    const gpu::bind_group_layout ui_frame_layout = ui->frame_bind_group_layout();
#if _DEBUG
    // The debug pass draws the overlay mesh proxies and the editor's ImGui
    // frame on top of the game UI, binding the scene pass's unjittered
    // camera group so the gizmos project without the TAA jitter.
    auto debug = std::make_unique<debug_draw::debug_pass>();
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

    // Register the built-in passes, stage by stage, through the same
    // add_pass anything else uses: the default chain is these
    // registrations, and within a stage the passes run in the order they
    // are added. Game or tool code places its own passes relative to
    // these stages and names (builtin_passes), and disables or removes
    // built-in ones, without an edit here.
    add_pass(std::move(shadow), pass_placement::in(render_stage::shadow));
    add_pass(std::move(point_shadow), pass_placement::in(render_stage::shadow));
    add_pass(std::move(spot_shadow), pass_placement::in(render_stage::shadow));
    add_pass(std::move(depth_pre), pass_placement::in(render_stage::scene));
    add_pass(std::move(scene), pass_placement::in(render_stage::scene));
    add_pass(std::move(skybox), pass_placement::in(render_stage::scene));
    // Motion vectors are computed from the finalised scene depth, before
    // the post chain consumes the colour.
    add_pass(std::move(velocity), pass_placement::in(render_stage::post));
    add_pass(std::move(volumetric_fog), pass_placement::in(render_stage::post));
    add_pass(std::move(motion_blur), pass_placement::in(render_stage::post));
    add_pass(std::move(bloom), pass_placement::in(render_stage::post));
    add_pass(std::move(auto_exposure), pass_placement::in(render_stage::post));
    add_pass(std::move(tonemap), pass_placement::in(render_stage::post));
    if (taa)
    {
        add_pass(std::move(taa), pass_placement::in(render_stage::post));
    }
    add_pass(std::move(fxaa), pass_placement::in(render_stage::post));
    add_pass(std::move(ui), pass_placement::in(render_stage::ui));
#if _DEBUG
    add_pass(std::move(debug), pass_placement::in(render_stage::overlay));
#endif

    // Start the post chain from the persisted values core::load_settings
    // resolved (settings.json, ALPHAENGINE_* variables, command line).
    // set_post_settings overwrites post_settings::taa.enabled with whether
    // the TAA pass runs.
    set_post_settings(startup_post_settings(services.post != nullptr ? *services.post : post_process_settings{}));

    // The swapchain image, the view's output and the colour-grading table
    // are valid at frame or view start without an in-frame producer, so
    // they are imported; every other resource a pass reads is produced by
    // a pass before it. The list is validated, and the GPU profiler sized
    // to it, here and again before the first frame after any later
    // change.
    m_passes.import_external(frame_resources::swapchain.name);
    m_passes.import_external(frame_resources::output.name);
    m_passes.import_external(frame_resources::grading_lut.name);
    rebuild_pass_list();

#if _DEBUG
    // Provide a couple of always-available reference gizmos (the infinite
    // ground grid + world axes) so a fresh debug build has something to
    // toggle from the overlay's Helpers panel. They join the world's
    // helper list on construction and draw through mesh proxies: the
    // infinite grid one the scene pass draws (depth-tested), the axes an
    // overlay one the always-on-top debug pass draws. Game code can add
    // the box / light / camera helpers against its own objects the same
    // way. The debug pass is dropped in release, so this whole block
    // compiles out there.
    m_debug_helpers.push_back(std::make_unique<debug_draw::infinite_grid>(*this));
    m_debug_helpers.push_back(std::make_unique<debug_draw::axes_helper>(*this));
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
    // GPU device they reference; their destructors destroy their proxies,
    // leave the helper list and free their geometry. Empty in release.
    // Game-owned helpers must likewise be released before quit.
    m_debug_helpers.clear();

    // The profiler's query sets go before the device does. Without a
    // device init never ran, and there is nothing to release.
    if (m_services.device != nullptr)
    {
        m_gpu_profiler.shutdown(*m_services.device);
    }

    // Drop the views' resource sets first — their off-screen targets and
    // the state every pass kept for them, the histories included — then
    // the passes — the built-in ones and any other code registered — and
    // what they published; the passes own per-frame bind-group layouts
    // referenced by the materials' pipelines.
    m_primary_view = nullptr;
    m_frame_views.clear();
    m_drawn_targets.clear();
    m_views.clear();
    m_view_sets.clear();
    m_passes.clear();
    m_frame_resources.clear();
    m_pass_list_changed = false;
    m_overlay = nullptr;
    m_frame_lights.clear();

    // The per-proxy buffers and bind groups the mesh and UI draws bind;
    // the skinned and UI groups were built against layouts the materials
    // own.
    if (m_services.device != nullptr)
    {
        m_mesh_draws.release(*m_services.device);
        m_ui_draws.release(*m_services.device);
    }

    // The grading LUT is a cached asset whose texture this handle keeps
    // alive; drop it while the device it is freed through is still up.
    m_grading_lut.reset();
    m_grading_lut_path.clear();

    // Then the material library, whose templates own pipelines that
    // reference the device. Release them before the device tears its
    // pools down.
    m_materials.quit();

    // Last, the world: withdraw the drawable aspect it hands to attaching
    // cameras, since there is no drawable left to match. Every pass and
    // helper that pointed into it is gone.
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

    // A pass added or removed since the last frame: validate the new
    // list and resize the GPU profiler to it before anything records.
    if (m_pass_list_changed)
    {
        rebuild_pass_list();
    }

    // Resolve a changed colour-grading LUT path through the asset cache
    // first, outside the frame like any other asset load (the upload is
    // ordered ahead of the frame that samples it). A no-op while the path
    // is unchanged.
    update_grading_lut();

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
    // The passes hold pointers into the world's proxies until render()
    // returns, however it returns.
    const world_frame_scope world_frame{m_world};

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

    // The mesh and UI proxies become this frame's draw lists, in proxy
    // order: the instance snapshots, joint palettes and UI quads the
    // extraction captured are uploaded here, with the frame open and
    // before any pass prepares.
    m_mesh_draws.build(m_world, gpu);
    m_ui_draws.build(m_world, gpu);

    // The views this frame renders, in order, each with its resource set:
    // the world picks the cameras once, here, so a camera destroyed or
    // disabled since the last frame is replaced by the runner-up without
    // any owner bookkeeping, and a view that drops out of the list takes
    // its targets and history with it.
    m_world.collect_views(m_views, m_drawable_width, m_drawable_height);
    sync_views();

    // What every view of the frame shares, captured once so no pass can
    // disagree about it mid-frame: the enabled lights, in the order the
    // scene pass packs them, the draw lists, the clock and the settings.
    frame_context frame{};
    m_world.collect_enabled_lights(m_frame_lights);
    frame.lights = m_frame_lights;
    frame.scene_draws = m_mesh_draws.scene_draws();
    frame.overlay_draws = m_mesh_draws.overlay_draws();
    frame.ui_draws = m_ui_draws.draws();
    frame.overlay = m_overlay;
    frame.world = &m_world;
    frame.frame_index = m_frame_index;
    // The engine clock ticked at the top of this frame; core::time reports
    // milliseconds, the shaders see seconds.
    if (m_services.time != nullptr)
    {
        frame.time_seconds = m_services.time->total_time() / 1000.0f;
        frame.delta_seconds = static_cast<float>(m_services.time->delta_time() / 1000.0);
    }
    frame.fog = m_world.fog();
    frame.depth_prepass = m_depth_prepass_enabled;
    m_post_settings.taa.enabled = temporal_aa_active();
    frame.post = m_post_settings;

    // The frame-global resources: the swapchain image and the grading
    // table. Every view's store falls back to this one, and the shadow
    // stage publishes its maps here.
    m_frame_resources.clear();
    m_frame_resources.publish(frame_resources::swapchain, gpu.swapchain_target());
    if (m_grading_lut != nullptr)
    {
        m_frame_resources.publish(frame_resources::grading_lut, m_grading_lut->texture);
    }

    // One encoder records the whole frame — each pass in a debug group
    // and, for the passes of the primary view and the once-per-frame
    // stages, between the profiler's timestamps — then submits. Within
    // each scope every pass prepares first, in list order: the per-frame
    // uploads, the culling and sorting, the bind-group rebuilds and every
    // cross-pass publish and lookup happen there, on this thread, before
    // anything of the scope is recorded — so the record walk only encodes
    // from finished state and the scene pass may hand its chunks to the
    // job pool's workers.
    auto encoder = gpu.create_command_encoder();
    m_gpu_profiler.begin_frame(*encoder);

    // The primary view — the last, the highest-ranked view on the
    // swapchain — lends its camera to the once-per-frame stages.
    const frame_context primary = view_context(frame, m_views.back(), *m_primary_view);

    // The shadow maps are rendered once and shared by every view, their
    // cascades fitted to the primary view's camera.
    frame_context shadow = primary;
    shadow.resources = &m_frame_resources;
    shadow.view = nullptr;
    m_passes.prepare(shadow, render_stage::shadow, render_stage::shadow);
    m_passes.record(*encoder, shadow, render_stage::shadow, render_stage::shadow, &m_gpu_profiler);

    // Every view renders its scene, post chain and UI in turn. Its store
    // starts with its own targets — the HDR scene target with its depth,
    // looked up from the target every frame so a resize that recreated it
    // reaches every depth consumer, and the LDR target — and its output:
    // the swapchain or its render texture, and its rectangle there, which
    // the first view to draw into a target clears whole. The passes publish
    // what they produce on top, as they prepare.
    m_drawn_targets.clear();
    for (std::size_t i = 0; i < m_views.size(); ++i)
    {
        const view& v = m_views[i];
        view_resources& resources = *m_frame_views[i];
        const frame_context ctx = view_context(frame, v, resources);
        resource_store& store = resources.resources();
        store.clear();
        store.set_fallback(&m_frame_resources);
        store.publish(frame_resources::scene_color, resources.scene_color());
        store.publish(frame_resources::scene_depth, resources.scene_depth());
        store.publish(frame_resources::ldr_color, resources.ldr_color());

        view_output output{};
        output.target = v.on_swapchain() ? gpu.swapchain_target() : v.target;
        output.x = v.x;
        output.y = v.y;
        output.width = v.width;
        output.height = v.height;
        output.target_width = v.target_width;
        output.target_height = v.target_height;
        output.first =
            std::find(m_drawn_targets.begin(), m_drawn_targets.end(), output.target) == m_drawn_targets.end();
        if (output.first)
        {
            m_drawn_targets.push_back(output.target);
        }
        store.publish(frame_resources::output, output);

        // The primary view's passes are the ones the profiler times; a UI
        // stage it skips is still bracketed, so every pass keeps its slot.
        const render_stage last = v.ui ? render_stage::ui : render_stage::post;
        pass_hooks* hooks = &resources == m_primary_view ? &m_gpu_profiler : nullptr;
        m_passes.prepare(ctx, render_stage::scene, last);
        m_passes.record(*encoder, ctx, render_stage::scene, last, hooks);
        if (hooks != nullptr && !v.ui)
        {
            m_passes.skip(*encoder, render_stage::ui, render_stage::ui, *hooks);
        }
        advance_history(ctx, resources);
    }

    // The tool overlay goes on top of everything, once, with the primary
    // view's camera and store.
    m_passes.prepare(primary, render_stage::overlay, render_stage::overlay);
    m_passes.record(*encoder, primary, render_stage::overlay, render_stage::overlay, &m_gpu_profiler);

    m_gpu_profiler.end_frame(*encoder);
    gpu.submit(std::move(encoder));
    ++m_frame_index;

    // Close the frame: the device presents the swapchain image it
    // acquired for this frame.
    gpu.end_frame();
    m_in_frame = false;
}

void rendering_engine::renderer::sync_views()
{
    auto& gpu = *m_services.device;

    // Each view's set, found by the view's camera or created; the targets
    // follow the view's size. A view is never empty: the world keeps the
    // swapchain view at least one pixel across.
    m_frame_views.clear();
    for (const view& v : m_views)
    {
        const auto found =
            std::find_if(m_view_sets.begin(),
                         m_view_sets.end(),
                         [&v](const std::unique_ptr<view_resources>& set) { return set->camera() == v.camera; });
        view_resources* set = found != m_view_sets.end() ? found->get() : nullptr;
        if (set == nullptr)
        {
            m_view_sets.push_back(std::make_unique<view_resources>(gpu, v.camera));
            set = m_view_sets.back().get();
        }
        set->resize(v.width, v.height);
        m_frame_views.push_back(set);
    }

    // A view that dropped out of the list takes its targets, its history and
    // every pass's state for it along; the device defers the frees until
    // the frames that used them have retired.
    m_view_sets.erase(std::remove_if(m_view_sets.begin(),
                                     m_view_sets.end(),
                                     [this](const std::unique_ptr<view_resources>& set) {
                                         return std::find(m_frame_views.begin(), m_frame_views.end(), set.get()) ==
                                                m_frame_views.end();
                                     }),
                      m_view_sets.end());
    m_primary_view = m_frame_views.back();
}

rendering_engine::frame_context
rendering_engine::renderer::view_context(const frame_context& frame, const view& v, view_resources& resources) const
{
    frame_context ctx = frame;
    ctx.active_camera = v.proxy;
    ctx.active_camera_handle = v.camera;
    ctx.view = &resources;
    ctx.resources = &resources.resources();
    ctx.viewport_width = v.width;
    ctx.viewport_height = v.height;

    // The temporal-AA jitter follows the view's own frame count and size,
    // so a view's sequence starts with the view and a resize rescales it
    // without any pass being told: the scene and skybox passes offset
    // their projection by it, the velocity pass subtracts it. Zero while
    // the TAA pass is not in the list or disabled.
    const view_history& history = resources.history();
    ctx.jitter =
        frame.post.taa.enabled ? taa_jitter_ndc(history.frames, v.width, v.height) : core::math::vec2{0.0f, 0.0f};
    ctx.prev_jitter = history.prev_jitter;
    // The view's history belongs to its camera, so its last
    // view-projection is only missing on its first camera frame and after
    // a camera-less one.
    ctx.has_prev_view_projection = history.has_prev_view_projection && v.proxy != nullptr;
    ctx.prev_view_projection = ctx.has_prev_view_projection ? history.prev_view_projection : core::math::mat4{};
    return ctx;
}

void rendering_engine::renderer::advance_history(const frame_context& ctx, view_resources& resources)
{
    // The unjittered view-projection (the scene pass applies the jitter on
    // top of the camera's own matrices), for the next frame's
    // reprojection. A camera-less frame leaves nothing to reproject
    // against.
    view_history& history = resources.history();
    if (ctx.active_camera != nullptr)
    {
        history.prev_view_projection = ctx.active_camera->projection * ctx.active_camera->view;
        history.has_prev_view_projection = true;
    }
    else
    {
        history.has_prev_view_projection = false;
    }
    history.prev_jitter = ctx.jitter;
    ++history.frames;
}

void rendering_engine::renderer::on_resize(uint32_t pixel_width, uint32_t pixel_height)
{
    // The listener that calls this runs from the window's event pump,
    // never from inside render(): the views of the frame being recorded
    // are sized already.
    assert(!m_in_frame && "renderer::on_resize must not run while a frame is being recorded");

    // A zero dimension is a minimised window; the main loop skips whole
    // frames until it is restored (and the restore reports the real size),
    // so the views keep their last usable size. A repeat of the live size
    // (the initial event, a DPI-only notification) changes nothing.
    if (pixel_width == 0 || pixel_height == 0)
    {
        return;
    }
    if (pixel_width == m_drawable_width && pixel_height == m_drawable_height)
    {
        return;
    }

    // The views on the swapchain are sized from the drawable at the next
    // render(), which rebuilds their targets (and every pass the state it
    // keeps at the view's size) then.
    m_drawable_width = pixel_width;
    m_drawable_height = pixel_height;

    // The projection follows the drawable so the image is not stretched:
    // the world's camera owners hand the new aspect to their cameras before
    // the next frame. Both dimensions are non-zero here, so the fallback is
    // never used.
    m_world.set_drawable_aspect(drawable_aspect_ratio(pixel_width, pixel_height, 1.0f));

    LOG_INF("Rendering Engine: drawable resized to %ux%u", pixel_width, pixel_height);
}

rendering_engine::gpu::device& rendering_engine::renderer::device() const
{
    assert(m_services.device != nullptr && "renderer::device is only valid between init and quit");
    return *m_services.device;
}

void rendering_engine::renderer::set_overlay(gpu::overlay_renderer* overlay)
{
    m_overlay = overlay;
}

rendering_engine::pass* rendering_engine::renderer::add_pass(std::unique_ptr<pass> p, const pass_placement& placement)
{
    assert(!m_in_frame && "renderer::add_pass must not run while a frame is being recorded");
    pass* added = m_passes.add(std::move(p), placement);
    if (added != nullptr)
    {
        m_pass_list_changed = true;
    }
    return added;
}

bool rendering_engine::renderer::remove_pass(std::string_view name)
{
    assert(!m_in_frame && "renderer::remove_pass must not run while a frame is being recorded");
    const pass* removed = m_passes.find(name);
    if (removed == nullptr)
    {
        return false;
    }
    // The state the pass kept for each view goes with it; what it published
    // names resources it released.
    m_frame_resources.clear();
    for (const std::unique_ptr<view_resources>& set : m_view_sets)
    {
        set->drop_state(*removed);
        set->resources().clear();
    }
    m_passes.remove(name);
    m_pass_list_changed = true;
    return true;
}

bool rendering_engine::renderer::set_pass_enabled(std::string_view name, bool enabled)
{
    assert(!m_in_frame && "renderer::set_pass_enabled must not run while a frame is being recorded");
    const bool was_enabled = m_passes.enabled(name);
    if (!m_passes.set_enabled(name, enabled))
    {
        return false;
    }
    if (was_enabled != enabled)
    {
        LOG_INF("Rendering Engine: pass '%.*s' %s",
                static_cast<int>(name.size()),
                name.data(),
                enabled ? "enabled" : "disabled");
    }
    return true;
}

bool rendering_engine::renderer::pass_enabled(std::string_view name) const
{
    return m_passes.enabled(name);
}

void rendering_engine::renderer::rebuild_pass_list()
{
    // A hazard is a pass reading a resource nothing before it produced: a
    // mis-ordered or mis-declared pass list, i.e. a programming error. It
    // stops a debug build here; a release build logs it (the list already
    // reported each offending read) and renders in the list's order.
    const bool hazard_free = m_passes.validate();
    assert(hazard_free && "pass list: a pass reads a resource before any pass produces it");
    if (!hazard_free)
    {
        LOG_ERR("Rendering Engine: the pass list has hazards; it is mis-declared or mis-ordered (see the pass_list "
                "errors above)");
    }

    // Per-pass GPU timings over the list, one slot per pass; disabled on
    // a device without timestamp queries. The old query sets are released
    // through the device's deferred destruction, so a frame still in
    // flight keeps writing its own.
    gpu::device& device = *m_services.device;
    m_gpu_profiler.shutdown(device);
    m_gpu_profiler.init(device, m_passes.pass_names());
    m_pass_list_changed = false;
}

bool rendering_engine::renderer::temporal_aa_active() const
{
    return m_passes.enabled(builtin_passes::taa);
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

void rendering_engine::renderer::set_post_settings(const post_settings& settings)
{
    m_post_settings = settings;

    // Whether temporal AA runs follows the TAA pass (registered at init
    // from graphics.temporal_aa, and enabled): a caller cannot flip it
    // from here, so the stored value always mirrors reality rather than
    // whatever was requested.
    m_post_settings.taa.enabled = temporal_aa_active();
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
    return m_primary_view != nullptr ? m_primary_view->scene_color().texture : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::scene_depth_texture() const
{
    return m_primary_view != nullptr ? m_primary_view->scene_depth() : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::ldr_color_texture() const
{
    return m_primary_view != nullptr ? m_primary_view->ldr_color().texture : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::velocity_texture() const
{
    return m_primary_view != nullptr ? m_primary_view->resources().get(frame_resources::velocity) : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::taa_resolve_texture() const
{
    return m_primary_view != nullptr ? m_primary_view->resources().get(frame_resources::taa_resolve) : gpu::texture{};
}

rendering_engine::gpu::texture rendering_engine::renderer::directional_shadow_map() const
{
    return m_frame_resources.get(frame_resources::directional_shadow).map;
}

rendering_engine::gpu::texture rendering_engine::renderer::spot_shadow_map() const
{
    return m_frame_resources.get(frame_resources::spot_shadow).map;
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
    // The skybox pass follows the world's environment from its next
    // prepare; mirror the choice onto every live standard material, so all
    // their surfaces pick up the matching image-based ambient.
    m_world.set_environment(env);
    m_materials.set_environment(env);
}

void rendering_engine::renderer::set_fog(const fog_settings& fog)
{
    m_world.set_fog(fog);
}
