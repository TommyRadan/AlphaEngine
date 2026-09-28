// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

#include <runtime/engine.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include <core/audio/audio.hpp>
#include <core/event_engine.hpp>
#include <core/input.hpp>
#include <core/job_pool.hpp>
#include <core/log.hpp>
#include <core/os/os.hpp>
#include <core/time.hpp>
#include <core/vfs/vfs.hpp>
#include <platform/audio_device.hpp>
#include <platform/platform.hpp>
#include <platform/window.hpp>
#include <rendering_engine/debug_draw/helper.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/gpu/shader_compiler.hpp>
#include <rendering_engine/gpu/surface.hpp>
#include <rendering_engine/renderer.hpp>
#include <rendering_engine/resources/asset_cache.hpp>
#include <runtime/components/animator_component.hpp>
#include <runtime/components/audio_listener_component.hpp>
#include <runtime/components/audio_source_component.hpp>
#include <runtime/engine_settings.hpp>
#include <runtime/game_module.hpp>
#include <runtime/overlay.hpp>
#include <runtime/physics/physics_debug_draw.hpp>
#include <runtime/physics/physics_world.hpp>
#include <runtime/reflection.hpp>
#include <runtime/render_extraction.hpp>
#include <runtime/scene.hpp>
#include <runtime/scene_manager.hpp>
#include <runtime/scripting/script_host.hpp>

namespace runtime
{
    namespace
    {
        // Published by the engine constructor, cleared by its destructor.
        engine* g_current_engine = nullptr;

        rendering_engine::gpu::backend_type to_backend_type(rendering_engine::graphics_backend b)
        {
            switch (b)
            {
            case rendering_engine::graphics_backend::vulkan:
                return rendering_engine::gpu::backend_type::vulkan;
            }
            throw std::logic_error{"to_backend_type: unknown graphics_backend"};
        }

        // The window as the GPU device sees it: its native handle and the
        // platform's Vulkan surface factory, the drawable's pixel size (the
        // logical settings size while the window cannot report one) and the
        // vsync preference.
        rendering_engine::gpu::surface_desc surface_for(const platform::window& window, const engine_settings& settings)
        {
            rendering_engine::gpu::surface_desc surface{};
            surface.native_window = window.sdl_window();
            surface.create_vulkan_surface = &platform::window::create_vulkan_surface;
            surface.destroy_vulkan_surface = &platform::window::destroy_vulkan_surface;
            surface.vulkan_instance_extensions = window.vulkan_instance_extensions();
            const platform::window_extent drawable = window.pixel_size();
            surface.width = drawable.width;
            surface.height = drawable.height;
            if (surface.width == 0 || surface.height == 0)
            {
                surface.width = settings.window.width;
                surface.height = settings.window.height;
            }
            surface.vsync = settings.window.vsync;
            return surface;
        }

        // The Vulkan loader does not look beside the executable for
        // explicit-layer JSON manifests by default. CI ships the validation
        // layer alongside AlphaEngine.exe (the Windows build job in ci.yml);
        // pointing VK_LAYER_PATH at the executable directory before the GPU
        // device creates its instance lets the loader discover
        // VkLayer_khronos_validation.json there. Skipped when VK_LAYER_PATH
        // is already set, so a user's own SDK install wins.
        void publish_bundled_layer_path()
        {
            if (core::os::environment_variable("VK_LAYER_PATH").has_value())
            {
                return;
            }
            // Without a trailing separator, so the loader's path
            // concatenation produces a well-formed lookup.
            const std::filesystem::path base_path = platform::base_path();
            if (base_path.empty() || !core::os::file_exists(base_path / "VkLayer_khronos_validation.json"))
            {
                return;
            }
            const std::string layer_path = core::os::path_to_utf8(base_path);
            platform::set_environment_variable("VK_LAYER_PATH", layer_path.c_str());
            LOG_INF("Published VK_LAYER_PATH=%s for bundled validation layer", layer_path.c_str());
        }

        // Runs @p fn(node&, C&) on every C the latest walk of each loaded
        // scene reached, scene by scene, in walk order.
        template<typename C, typename Fn>
        void each_visited(scene_manager& scenes, Fn fn)
        {
            for (std::size_t index = 0; index < scenes.scene_count(); ++index)
            {
                scenes.scene_at(index).each_visited<C>(fn);
            }
        }

        // Logs what the type registry holds once every registration has run:
        // the engine's built-in types and those of the game modules linked
        // into this executable.
        void log_registered_types()
        {
            const char* const category_names[] = {"components", "behaviours", "objects"};
            std::size_t counts[3] = {};
            std::string names[3];
            for (const type_info* type : default_type_registry().types())
            {
                const auto category = static_cast<std::size_t>(type->category);
                ++counts[category];
                names[category] += names[category].empty() ? type->name : ", " + type->name;
            }
            LOG_INF("Type registry: %zu %s, %zu %s, %zu %s",
                    counts[0],
                    category_names[0],
                    counts[1],
                    category_names[1],
                    counts[2],
                    category_names[2]);
            for (std::size_t category = 0; category < 3; ++category)
            {
                LOG_DBG("Type registry %s: %s", category_names[category], names[category].c_str());
            }
        }
    } // namespace

    engine& current_engine()
    {
        if (g_current_engine == nullptr)
        {
            // Loud in every configuration: an assert would vanish in Release
            // and let the caller dereference null. A caller in a destructor
            // (a module static unwinding after the engine) terminates here,
            // which is still a clear stack rather than a silent corruption.
            LOG_FTL("current_engine() called with no live engine");
            throw std::logic_error{"current_engine() called with no live engine"};
        }
        return *g_current_engine;
    }

    engine::engine(engine_settings values)
    {
        // Install ourselves first so subsystem constructors can observe
        // the engine.
        if (g_current_engine != nullptr)
        {
            LOG_FTL("engine: another instance is already live");
            throw std::logic_error{"engine: another instance is already live"};
        }
        g_current_engine = this;

        // Construction order mirrors the declaration order in the header.
        // The settings arrive resolved (defaults, file, environment, command
        // line — see core::load_settings), so every subsystem below reads a
        // final value.
        settings = std::make_unique<engine_settings>(std::move(values));
        time = std::make_unique<core::time>();
        time->set_time_scale(static_cast<double>(settings->time.scale));
        // The frame's schedule; the engine adds its own systems in init(),
        // once every subsystem they run is up.
        systems = std::make_unique<runtime::scheduler>(*time);
        // The worker pool has no dependencies and is brought up early so any
        // subsystem can hand it work during init or per frame. Its threads
        // idle until the first job is dispatched.
        jobs = std::make_unique<core::job_pool>();
        events = std::make_unique<core::event_bus>();
        // The mixer plays and decodes through the platform's audio device.
        audio = std::make_unique<core::audio>(std::make_unique<platform::sdl_audio_output>(),
                                              std::make_unique<platform::sdl_audio_decoder>());
        // Maps physical input to actions and axes from the same raw events the window will emit once it starts
        // pumping OS events; it only needs the bus, so it can be constructed here, ahead of the window.
        input = std::make_unique<core::input>();
        window = std::make_unique<platform::window>();
        gpu = rendering_engine::gpu::create_device(to_backend_type(settings->graphics.backend));
        // The asset cache hands out GPU-resource-backed handles, so it is
        // constructed after the device; its loaders are only usable once the
        // device is brought up in init().
        assets = std::make_unique<rendering_engine::asset_cache>();
        // Asynchronous loads decode on the worker pool; the pool outlives the
        // cache (it is destroyed after it, below), and the cache waits for
        // its in-flight decodes before it goes.
        assets->set_jobs(jobs.get());
        // The built-in materials inside @c renderer are deferred
        // until init() because they build shader modules and pipelines
        // on the device, which needs the window to be live first.
        renderer = std::make_unique<rendering_engine::renderer>();
        physics = std::make_unique<runtime::physics::world>();
        scripts = std::make_unique<runtime::script_host>();
        // Every scene the manager creates feeds this renderer's world: its
        // render_world member exists from construction (renderer::init has
        // not built the passes yet, but the world holds no GPU resource),
        // so components can attach to it before the engine finishes
        // starting up.
        scenes = std::make_unique<runtime::scene_manager>(renderer->world());
    }

    engine::~engine()
    {
        // Tear down in reverse construction order, then clear the
        // current-engine pointer last so any destructor side effects
        // that reach for current_engine() still see a valid engine. The
        // overlay sits on top of every subsystem, so it goes first.
        m_overlay.reset();
        scenes.reset();
        scripts.reset();
        m_physics_debug.reset();
        physics.reset();
        renderer.reset();
        // Destroyed after its consumers (renderer / scenes) so their handles
        // are already released, and before the gpu device so any asset still
        // alive can free its GPU resource against a live device.
        assets.reset();
        gpu.reset();
        window.reset();
        // Reverse of construction order: input was made after audio, so it goes first here.
        input.reset();
        audio.reset();
        m_quit_subscription.reset();
        events.reset();
        // Joins the worker threads. Every per-frame job is forked and joined
        // within tick(), so nothing is in flight by the time we get here.
        jobs.reset();
        systems.reset();
        time.reset();
        settings.reset();
        g_current_engine = nullptr;
    }

    void engine::init()
    {
        events->init();
        // No renderer/VFS dependency: opens (or gracefully declines) the
        // playback device up front so a module's on_engine_start can play a
        // sound immediately.
        audio->init();
        // Subscribes to the raw input events the window will start emitting once it is up, and picks up any
        // `input.bindings` rebind from settings ahead of the game modules' bind_action / bind_axis calls below.
        input->init(*events, settings->input);
        // The per-user rebinds a player saved at runtime (core::input::save_user_bindings), read back the same
        // way before any bind_action / bind_axis call so they land on top of the compiled and settings.json
        // defaults as each name is registered.
        if (const std::filesystem::path pref_path = platform::pref_path("AlphaEngine", "AlphaEngine");
            !pref_path.empty())
        {
            input->load_user_bindings(pref_path / "input_bindings.json");
        }

        // Mount the content root before anything loads a file: the configured
        // directory when one is set, else the discovered default beside the
        // executable (or in one of its parents).
        const std::filesystem::path content_root =
            settings->content.root.empty() ? platform::content_root() : core::os::utf8_path(settings->content.root);
        LOG_INF("Content root: %s", core::os::path_to_utf8(content_root).c_str());
        core::default_vfs().mount_directory(content_root);

        // The window first, then the gpu device against its surface, then
        // the renderer, which builds its passes and materials on the live
        // device. The renderer is handed every subsystem and setting it
        // reads; it loads through the asset cache only from its first frame,
        // after the cache is initialised below.
        window->init(settings->window);
        // What the GPU device reads from the process before it comes up: the
        // per-user directory its shader and pipeline caches live in, and the
        // loader's layer path when a validation layer ships beside the
        // executable.
        if (const std::filesystem::path pref_path = platform::pref_path("AlphaEngine", "AlphaEngine");
            !pref_path.empty())
        {
            rendering_engine::gpu::set_default_shader_cache_directory(pref_path / "shader_cache");
        }
        publish_bundled_layer_path();
        gpu->init(surface_for(*window, *settings), settings->graphics.frames_in_flight);
        rendering_engine::render_services render{};
        render.device = gpu.get();
        const platform::window_extent drawable = window->pixel_size();
        render.drawable_width = drawable.width;
        render.drawable_height = drawable.height;
        render.fallback_aspect = settings->window.aspect_ratio();
        render.events = events.get();
        render.jobs = jobs.get();
        render.assets = assets.get();
        render.time = time.get();
        render.graphics = &settings->graphics;
        render.shadows = &settings->shadows;
        render.post = &settings->post;
        renderer->init(render);
        // The overlay draws through the window, the device and the
        // renderer's debug pass, all live from here on.
        if (m_overlay != nullptr)
        {
            m_overlay->init(*this);
        }
        // The asset cache's loaders need a live device, so it is initialised
        // right after the renderer, handed the device its uploads (and every
        // asset's release) go to and the renderer its glTF materials are
        // built through.
        assets->init(*gpu, *renderer);
#if _DEBUG
        // Debug builds reload a texture whose file under the content root
        // changes on disk (polled from assets->pump()).
        assets->enable_hot_reload(content_root);
#endif
        physics->init();
#if _DEBUG
        // After the renderer and the world: the line helper that draws the
        // world's colliders.
        m_physics_debug = std::make_unique<runtime::physics::debug_draw>(*renderer, *physics);
#endif
        // Before the scenes, so the game modules' bootstraps and scene files
        // can attach scripted behaviours; scripts read through the VFS
        // mounted above.
        scripts->init(*systems);
        scenes->init();

        // Every subsystem the frame runs is up: schedule it.
        add_engine_systems();

        // Register our own quit_requested listener now that the event
        // bus is initialised.
        m_quit_subscription =
            events->subscribe<core::quit_requested>([this](const core::quit_requested&) { m_quit_requested = true; });

        // Every subsystem is up: install the game. Each game module
        // registered its bootstrap at static-init time (before the engine
        // existed); run them now so they spawn their nodes and behaviours
        // into the active scene, where the scenes own and tear them down.
        log_registered_types();
        install_game_modules(scenes->active_scene());
    }

    void engine::quit()
    {
        // Nothing runs a frame from here on; the systems go before the
        // subsystems they call into.
        m_engine_systems.clear();
        // Scenes first: freeing their nodes unwinds every component's
        // renderer, light and camera registration and releases its GPU
        // buffers, which needs the renderer and the asset cache still up.
        scenes->quit();
        // Every scripted behaviour went with its scene; close the Lua state.
        scripts->quit();
        // Every physics component has unregistered with its scene; the world
        // and its debug helper go before the renderer the helper draws
        // through.
        m_physics_debug.reset();
        physics->quit();
        // Scene teardown is where the bulk of the asset handles drop; reclaim
        // the index slots they leave behind before the cache itself goes.
        const std::size_t swept = assets->collect_unused();
        if (swept != 0)
        {
            LOG_INF("Asset cache: swept %zu expired entries at scene teardown", swept);
        }
        assets->quit();
        // The overlay goes while the window, the device and the renderer
        // it draws through are still up.
        if (m_overlay != nullptr)
        {
            m_overlay->shutdown();
        }
        renderer->quit();
        // Reverse of init: the device goes before the window its surface
        // was created on.
        gpu->quit();
        window->quit();
        core::default_vfs().unmount_all();
        input->quit();
        audio->quit();
        m_quit_subscription.reset();
        events->quit();
    }

    void engine::add_engine_systems()
    {
        namespace order = engine_order;
        // Whether a system runs while the game is paused.
        constexpr bool always = true;
        constexpr bool game = false;
        const auto add = [this](stage where, const char* name, int at, bool while_paused, auto run)
        {
            m_engine_systems.push_back(systems->add(
                where, name, scheduler::system_function{std::move(run)}, {.order = at, .while_paused = while_paused}));
        };

        // Input: pump OS input once per rendered frame (variable rate),
        // latch this frame's cursor motion now that every pumped event has
        // updated the live action / axis state, deliver the events buffered
        // through event_bus::enqueue since the last frame, then upload the
        // asynchronous asset loads whose decodes have landed, so a texture
        // that finished decoding is drawn this frame.
        add(stage::input,
            "window_events",
            order::window_events,
            always,
            [this](const frame_time&) { window->tick(*events); });
        add(stage::input, "input_frame", order::input_frame, always, [this](const frame_time&) { input->end_frame(); });
        add(stage::input, "event_queue", order::event_queue, always, [this](const frame_time&) { events->flush(); });
        add(stage::input, "asset_uploads", order::asset_uploads, always, [this](const frame_time&) { assets->pump(); });

        // The fixed step: latch the step's pressed / released edges before
        // any game logic sees them, run every scene's on_fixed_update in
        // hierarchy order, then simulate — so forces and moves made in the
        // step's game logic apply to it; contact events are dispatched
        // before the next step — then advance the animators, and apply what
        // the step's hooks queued, so a node destroyed in on_fixed_update
        // loses its body before the next step.
        add(stage::scripts_fixed,
            "input_step",
            order::input_step,
            game,
            [this](const frame_time&) { input->begin_step(); });
        add(stage::scripts_fixed,
            "fixed_update",
            order::fixed_update,
            game,
            [this](const frame_time&) { scenes->fixed_update(); });
        add(stage::physics,
            "physics_step",
            order::physics_step,
            game,
            [this](const frame_time& frame) { physics->step(frame.delta); });
        add(stage::post_physics,
            "animation_step",
            order::animation_step,
            game,
            [this](const frame_time& frame)
            {
                each_visited<animator_component>(
                    *scenes, [&frame](node&, animator_component& animator) { animator.advance(frame.delta); });
            });
        add(stage::post_physics,
            "deferred_commands",
            order::deferred_commands,
            always,
            [this](const frame_time&) { scenes->apply_deferred(); });

        // The frame update: place the simulated nodes between the last two
        // physics steps, so they move smoothly at the render rate (not while
        // paused, so the tools can move them); run every scene's on_update;
        // apply what it queued.
        add(stage::update,
            "physics_interpolation",
            order::physics_interpolation,
            game,
            [this](const frame_time& frame) { physics->interpolate(static_cast<float>(frame.alpha)); });
        add(stage::update, "scene_update", order::scene_update, game, [this](const frame_time&) { scenes->update(); });
        add(stage::update,
            "deferred_commands",
            order::deferred_commands,
            always,
            [this](const frame_time&) { scenes->apply_deferred(); });

        // The animators write their poses, between the last two fixed steps;
        // then every pose moved by the update and the animation settles.
        add(stage::animation,
            "animation",
            order::animation,
            game,
            [this](const frame_time& frame)
            {
                each_visited<animator_component>(
                    *scenes, [&frame](node&, animator_component& animator) { animator.apply(frame.alpha); });
            });
        add(stage::transform_propagation,
            "transform_propagation",
            order::transform_propagation,
            always,
            [this](const frame_time&) { scenes->propagate_transforms(); });

        // Audio, from the settled poses; independent of the drawable, so it
        // keeps playing while the window is minimized. The mixer runs on
        // real time and plays its voices at the time scale.
        add(stage::audio,
            "audio_poses",
            order::audio_poses,
            always,
            [this](const frame_time&)
            {
                each_visited<audio_listener_component>(
                    *scenes, [](node& owner, audio_listener_component& listener) { listener.sync(owner); });
                each_visited<audio_source_component>(
                    *scenes, [](node& owner, audio_source_component& source) { source.sync(owner); });
            });
        add(stage::audio,
            "audio_mix",
            order::audio_mix,
            always,
            [this](const frame_time& frame)
            {
                audio->set_time_scale(static_cast<float>(frame.time_scale));
                audio->update(frame.unscaled_delta);
            });

        // Render extraction, for a frame that is drawn: build the overlay
        // (which may edit nodes too), then write the meshes, UI elements,
        // lights and cameras the renderer reads into its world, once, from
        // that final state, then have the debug helpers rebuild what they
        // follow from it — the proxies just written, the physics world's
        // lines — so the renderer reads neither the scene nor the physics
        // world.
        add(stage::render_extract,
            "overlay",
            order::overlay,
            always,
            [this](const frame_time&)
            {
                if (m_overlay != nullptr)
                {
                    m_overlay->begin_frame();
                }
            });
        add(stage::render_extract,
            "render_proxies",
            order::render_proxies,
            always,
            [this](const frame_time&) { extract_render_proxies(*scenes); });
        add(stage::render_extract,
            "debug_helpers",
            order::debug_helpers,
            always,
            [this](const frame_time&) { rendering_engine::debug_draw::update_helpers(renderer->world()); });
    }

    void engine::tick()
    {
        // Advance the clock first so its deltas describe the frame about to
        // be processed — the time since the previous tick — rather than the
        // one before it. The first tick reports a zero delta (see
        // core::time), so frame one runs no fixed step and carries a zero
        // render delta; frame two carries frame one's real duration.
        time->perform_tick();

        // Input, every fixed step the clock drains, update, animation,
        // transform propagation and audio (see the list in engine.hpp).
        systems->run_frame();

        // A minimized window has no drawable (the Vulkan surface reports a
        // zero extent), so the frame is neither extracted nor presented
        // until the window is restored; the stages above keep running.
        if (!window->is_minimized())
        {
            systems->run_render_extract();
            // The device presents the frame at the end of renderer->render().
            renderer->render();

            // Counts rendered frames only, so a run stuck minimized never reaches its limit.
            const unsigned int frame_limit = settings->diagnostics.frame_limit;
            if (frame_limit != 0 && ++m_frames_rendered >= frame_limit)
            {
                LOG_INF("Frame limit of %u reached; requesting quit", frame_limit);
                events->emit<core::quit_requested>();
            }
        }
    }

    void engine::broadcast_engine_start()
    {
        events->emit<core::engine_start>();
    }

    void engine::broadcast_engine_stop()
    {
        events->emit<core::engine_stop>();
    }

    bool engine::is_quit_requested() const noexcept
    {
        return m_quit_requested;
    }

    void engine::set_overlay(std::unique_ptr<overlay> value)
    {
        m_overlay = std::move(value);
    }
} // namespace runtime
