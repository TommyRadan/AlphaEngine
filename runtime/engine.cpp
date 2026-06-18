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

#include <cassert>
#include <stdexcept>

#include <core/event_engine.hpp>
#include <core/jobs.hpp>
#include <core/log.hpp>
#include <core/settings.hpp>
#include <core/time.hpp>
#include <rendering_engine/assets/asset_cache.hpp>
#include <rendering_engine/assets/asset_device.hpp>
#include <rendering_engine/debug_ui/imgui_layer.hpp>
#include <rendering_engine/gpu/device.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/window.hpp>
#include <runtime/scene_graph.hpp>

// Defined in external/api/game_module.cpp. Flushes GAME_MODULE()
// registrations that were queued at static-init time onto the live
// event bus.
extern void install_pending_game_modules();

namespace runtime
{
    namespace
    {
        // Published by the engine constructor, cleared by its destructor.
        // Accessed by every translation unit that used to reach for the
        // old singleton::get_instance() hooks.
        engine* g_current_engine = nullptr;

        rendering_engine::gpu::backend_type to_backend_type(graphics_backend b)
        {
            switch (b)
            {
            case graphics_backend::opengl:
                return rendering_engine::gpu::backend_type::opengl;
            case graphics_backend::vulkan:
                return rendering_engine::gpu::backend_type::vulkan;
            }
            throw std::logic_error{"to_backend_type: unknown graphics_backend"};
        }
    } // namespace

    engine& current_engine()
    {
        assert(g_current_engine != nullptr && "current_engine() called with no live engine");
        return *g_current_engine;
    }

    engine::engine()
    {
        // Install ourselves first so subsystem constructors can observe
        // the engine (for example, settings reads config, time queries
        // SDL).
        if (g_current_engine != nullptr)
        {
            LOG_FTL("engine: another instance is already live");
            throw std::logic_error{"engine: another instance is already live"};
        }
        g_current_engine = this;

        // Construction order mirrors the declaration order in the
        // header and the old subsystem init order in main_loop.cpp.
        settings = std::make_unique<::settings>();
        time = std::make_unique<core::time>();
        // The worker pool has no dependencies and is brought up early so any
        // subsystem can hand it work during init or per frame. Its threads
        // idle until the first job is dispatched.
        jobs = std::make_unique<core::jobs>();
        events = std::make_unique<core::event_bus>();
        window = std::make_unique<rendering_engine::window>();
        gpu = rendering_engine::gpu::create_device(to_backend_type(settings->graphics.backend));
        // The asset cache hands out GPU-resource-backed handles, so it is
        // constructed after the device; its loaders are only usable once the
        // device is brought up in init().
        assets = std::make_unique<rendering_engine::asset_cache>();
        // The built-in materials inside @c renderer are deferred
        // until init() because they compile GL shader programs and
        // need the GL context to be live first.
        renderer = std::make_unique<rendering_engine::context>();
        scenes = std::make_unique<runtime::context>();
    }

    engine::~engine()
    {
        // Tear down in reverse construction order, then clear the
        // current-engine pointer last so any destructor side effects
        // that reach for current_engine() still see a valid engine.
        scenes.reset();
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

        // Game modules self-register at static-init time (before the
        // engine existed) — wire those queued registrations onto the
        // now-live event bus before anything broadcasts.
        install_pending_game_modules();

        renderer->init();
        // The renderer brings the gpu device up, so the asset cache — whose
        // loaders need a live device — is initialised right after it. Publish
        // the live device to the asset layer first, so the cache and the
        // reference-counted asset handles resolve it without reaching into the
        // engine global.
        rendering_engine::set_asset_device(gpu.get());
        assets->init();
        scenes->init();

        // Register our own quit_requested listener now that the event
        // bus is initialised.
        events->subscribe<core::quit_requested>([this](const core::quit_requested&) { m_quit_requested = true; });
    }

    void engine::quit()
    {
        scenes->quit();
        assets->quit();
        renderer->quit();
        events->quit();
    }

    void engine::tick()
    {
        // Pump OS input once per rendered frame (variable rate). Input
        // state set here is read by the fixed-step updates below.
        window->tick();

        // Fixed-step update, decoupled from the render rate. Feed the time
        // elapsed since the previous frame into the accumulator, then drain
        // it one fixed step at a time — running game logic zero, one, or
        // several times this frame so simulation behaviour is independent of
        // frame rate. The accumulator's clamp bounds the step count, so this
        // loop always terminates (the spiral-of-death guard lives in time).
        time->accumulate(time->delta_time());
        core::frame frame;
        frame.m_delta_time = static_cast<float>(time->fixed_delta_time());
        while (time->next_fixed_step())
        {
            events->emit<core::frame>(frame);
        }

        // Build the ImGui debug overlay before the passes run; its draw
        // data is recorded inside the swapchain-targeted debug pass (via
        // the render_debug event) so it composites on top of the frame on
        // both the OpenGL and Vulkan backends. No-op in release builds.
        rendering_engine::debug_ui::begin_frame();
        // Propagate scene-graph component updates (light/camera poses tracking
        // their nodes) after the fixed updates moved nodes and before the draw
        // walk. Runs once per rendered frame; render_* events fire per render
        // inside renderer->render(). The interpolation alpha for smoothing
        // between fixed states is available via time->interpolation_alpha().
        scenes->update();
        renderer->render();
        window->swap_buffers();
        time->perform_tick();
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
