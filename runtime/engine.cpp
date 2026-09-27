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

#include <runtime/engine.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <utility>

#include <core/audio/audio.hpp>
#include <core/event_engine.hpp>
#include <core/input.hpp>
#include <core/jobs.hpp>
#include <core/log.hpp>
#include <core/platform/platform.hpp>
#include <core/settings.hpp>
#include <core/time.hpp>
#include <core/vfs/vfs.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/debug_ui/imgui_layer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/game_module.hpp>
#include <runtime/physics/physics_world.hpp>
#include <runtime/scene_manager.hpp>

namespace runtime
{
    namespace
    {
        // Published by the engine constructor, cleared by its destructor.
        // Accessed by every translation unit that used to reach for the
        // old singleton::get_instance() hooks.
        engine* g_current_engine = nullptr;

        rendering_engine::gpu::backend_type to_backend_type(core::graphics_backend b)
        {
            switch (b)
            {
            case core::graphics_backend::opengl:
                return rendering_engine::gpu::backend_type::opengl;
            case core::graphics_backend::vulkan:
                return rendering_engine::gpu::backend_type::vulkan;
            }
            throw std::logic_error{"to_backend_type: unknown graphics_backend"};
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

    engine::engine(core::settings values)
    {
        // Install ourselves first so subsystem constructors can observe
        // the engine (for example, time queries SDL).
        if (g_current_engine != nullptr)
        {
            LOG_FTL("engine: another instance is already live");
            throw std::logic_error{"engine: another instance is already live"};
        }
        g_current_engine = this;

        // Construction order mirrors the declaration order in the
        // header and the old subsystem init order in main_loop.cpp.
        // The settings arrive resolved (defaults, file, environment, command
        // line — see core::load_settings), so every subsystem below reads a
        // final value.
        settings = std::make_unique<core::settings>(std::move(values));
        time = std::make_unique<core::time>();
        // The worker pool has no dependencies and is brought up early so any
        // subsystem can hand it work during init or per frame. Its threads
        // idle until the first job is dispatched.
        jobs = std::make_unique<core::jobs>();
        events = std::make_unique<core::event_bus>();
        audio = std::make_unique<core::audio>();
        // Maps physical input to actions and axes from the same raw events the window will emit once it starts
        // pumping SDL; it only needs the bus, so it can be constructed here, ahead of the window.
        input = std::make_unique<core::input>();
        window = std::make_unique<rendering_engine::window>();
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
        // until init() because they compile GL shader programs and
        // need the GL context to be live first.
        renderer = std::make_unique<rendering_engine::context>();
        physics = std::make_unique<runtime::physics::world>();
        scenes = std::make_unique<runtime::scene_manager>();
    }

    engine::~engine()
    {
        // Tear down in reverse construction order, then clear the
        // current-engine pointer last so any destructor side effects
        // that reach for current_engine() still see a valid engine.
        scenes.reset();
        physics.reset();
        renderer.reset();
        // Destroyed after its consumers (renderer / scenes) so their handles
        // are already released, and before the gpu device so any asset still
        // alive can free its GPU resource against a live device.
        assets.reset();
        // The asset layer no longer has a device to free against once the
        // device is gone; clear the accessor before destroying it.
        rendering_engine::set_asset_device(nullptr);
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
        time.reset();
        settings.reset();
        g_current_engine = nullptr;
    }

    void engine::init()
    {
        // Matches the old main_loop.cpp init sequence: events,
        // rendering, scene graph. The window/GL context is brought up
        // inside rendering_engine::context::init(); it in turn
        // constructs the built-in passes and materials once GL is
        // alive.
        events->init();
        // No renderer/VFS dependency: opens (or gracefully declines) the
        // playback device up front so a module's on_engine_start can play a
        // sound immediately.
        audio->init();
        // Subscribes to the raw input events the window will start emitting once it is up, and picks up any
        // `input.bindings` rebind from settings ahead of the game modules' bind_action / bind_axis calls below.
        input->init(*events, settings->input);

        // Mount the asset root before anything loads a file: the configured
        // directory when one is set, else the discovered default beside the
        // executable (or in one of its parents).
        const std::filesystem::path asset_root = settings->assets.root.empty()
                                                     ? core::platform::asset_root()
                                                     : core::platform::utf8_path(settings->assets.root);
        LOG_INF("Asset root: %s", core::platform::path_to_utf8(asset_root).c_str());
        core::default_vfs().mount_directory(asset_root);

        renderer->init();
        // The renderer brings the gpu device up, so the asset cache — whose
        // loaders need a live device — is initialised right after it. Publish
        // the live device to the asset layer first, so the cache and the
        // reference-counted asset handles resolve it without reaching into the
        // engine global.
        rendering_engine::set_asset_device(gpu.get());
        assets->init();
        // After the renderer: debug builds give the physics world a line
        // helper that draws its colliders.
        physics->init();
        scenes->init();

        // Register our own quit_requested listener now that the event
        // bus is initialised.
        m_quit_subscription =
            events->subscribe<core::quit_requested>([this](const core::quit_requested&) { m_quit_requested = true; });

        // Every subsystem is up: install the game. Each game module
        // registered its bootstrap at static-init time (before the engine
        // existed); run them now so they spawn their nodes and behaviours
        // into the active scene, where the scenes own and tear them down.
        install_game_modules(scenes->active_scene());
    }

    void engine::quit()
    {
        // Scenes first: freeing their nodes unwinds every component's
        // renderer, light and camera registration and releases its GPU
        // buffers, which needs the renderer and the asset cache still up.
        scenes->quit();
        // Every physics component has unregistered with its scene; the world
        // goes before the renderer its debug helper draws through.
        physics->quit();
        // Scene teardown is where the bulk of the asset handles drop; reclaim
        // the index slots they leave behind before the cache itself goes.
        const std::size_t swept = assets->collect_unused();
        if (swept != 0)
        {
            LOG_INF("Asset cache: swept %zu expired entries at scene teardown", swept);
        }
        assets->quit();
        renderer->quit();
        core::default_vfs().unmount_all();
        input->quit();
        audio->quit();
        m_quit_subscription.reset();
        events->quit();
    }

    void engine::tick()
    {
        // Advance the clock first so delta_time() describes the frame about
        // to be processed — the time since the previous tick — rather than
        // the one before it. The first tick reports a zero delta (see
        // core::time), so frame one runs no fixed step and carries a zero
        // render delta; frame two carries frame one's real duration.
        time->perform_tick();

        // Pump OS input once per rendered frame (variable rate). Input
        // state set here is read by the fixed-step updates below.
        window->tick();
        // Latches this frame's cursor motion (see core::input::mouse_delta) now that every event window->tick()
        // pumped has updated the live action / axis state; nothing changes it again before the next window->tick().
        input->end_frame();

        // Deliver the events buffered through event_bus::enqueue since the
        // last tick, now that this frame's input has been pumped and before
        // the fixed-step updates consume it.
        events->flush();

        // Resolve the asynchronous asset loads whose decodes have landed —
        // the device upload happens here, on the main thread — so a texture
        // that finished decoding is drawn this frame.
        assets->pump();

        // Fixed-step update, decoupled from the render rate. Feed the time
        // elapsed since the previous frame into the accumulator, then drain
        // it one fixed step at a time — running game logic zero, one, or
        // several times this frame so simulation behaviour is independent of
        // frame rate. The accumulator's clamp bounds the step count, so this
        // loop always terminates (the spiral-of-death guard lives in time).
        // Behaviours receive their on_fixed_update from this core::frame.
        time->accumulate(time->delta_time());
        core::frame frame;
        frame.m_delta_time = static_cast<float>(time->fixed_delta_time());
        while (time->next_fixed_step())
        {
            // Latches was_action_pressed / was_action_released for this step before behaviours see it, so they
            // stay stable for every on_fixed_update the step runs (core::input::begin_step).
            input->begin_step();
            events->emit<core::frame>(frame);
            // The simulation advances after the step's game logic, so forces
            // and moves made in it apply to this step; contact events are
            // dispatched before the next one.
            physics->step(static_cast<float>(time->fixed_delta_time() / 1000.0));
        }
        // Place the simulated nodes between the last two physics steps, so
        // they move smoothly at the render rate.
        physics->interpolate(static_cast<float>(time->interpolation_alpha()));

        // Per-render-frame update for visual / input-driven game logic, carrying
        // the variable render delta so it stays smooth at the render rate rather
        // than stepping at the fixed rate above. Emitted before the scene-graph
        // update so any node poses it moves are picked up this frame.
        core::render_update render_tick;
        render_tick.m_delta_time = static_cast<float>(time->delta_time());
        events->emit<core::render_update>(render_tick);

        // Propagate scene-graph component updates (light/camera poses tracking
        // their nodes, behaviours' on_update) in every loaded scene after the
        // fixed updates moved nodes and before the draw walk. Runs once per
        // rendered frame; render_* events fire per render inside
        // renderer->render(). The interpolation alpha for smoothing between
        // fixed states is available via time->interpolation_alpha().
        scenes->update();

        // Mixed after the scene graph so every source/listener transform
        // pushed by this frame's component updates is already applied.
        // Independent of the drawable, so it keeps playing while minimized.
        audio->update(static_cast<float>(time->delta_time()));

        // A minimized window has no drawable (the Vulkan surface reports a
        // zero extent), so the frame is neither built nor presented until
        // the window is restored; the updates above keep running.
        if (!window->is_minimized())
        {
            // Build the ImGui debug overlay before the passes run; its draw
            // data is recorded inside the swapchain-targeted debug pass
            // (debug_ui::record_draw_data) so it composites on top of the
            // frame on both the OpenGL and Vulkan backends. No-op in release
            // builds.
            rendering_engine::debug_ui::begin_frame();
            renderer->render();
            window->swap_buffers();
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
} // namespace runtime
