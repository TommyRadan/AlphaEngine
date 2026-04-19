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

#include <control/engine.hpp>

#include <cassert>
#include <stdexcept>

#include <event_engine/event_engine.hpp>
#include <infrastructure/log.hpp>
#include <infrastructure/settings.hpp>
#include <infrastructure/time.hpp>
#include <rendering_engine/opengl/opengl.hpp>
#include <rendering_engine/renderers/basic_renderer.hpp>
#include <rendering_engine/renderers/overlay_renderer.hpp>
#include <rendering_engine/rendering_engine.hpp>
#include <rendering_engine/window.hpp>
#include <scene_graph/scene_graph.hpp>

// Defined in external/api/game_module.cpp. Flushes GAME_MODULE()
// registrations that were queued at static-init time onto the live
// event bus.
extern void install_pending_game_modules();

namespace control
{
    namespace
    {
        // Published by the engine constructor, cleared by its destructor.
        // Accessed by every translation unit that used to reach for the
        // old singleton::get_instance() hooks.
        engine* g_current_engine = nullptr;
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
        time = std::make_unique<infrastructure::time>();
        events = std::make_unique<event_engine::context>();
        window = std::make_unique<rendering_engine::window>();
        opengl = std::make_unique<rendering_engine::opengl::context>();
        // basic_renderer_ / overlay_renderer_ are deferred until init()
        // because they compile GL shader programs and need the GL
        // context to be live first.
        renderer = std::make_unique<rendering_engine::context>();
        scenes = std::make_unique<scene_graph::context>();
    }

    engine::~engine()
    {
        // Tear down in reverse construction order, then clear the
        // current-engine pointer last so any destructor side effects
        // that reach for current_engine() still see a valid engine.
        scenes.reset();
        renderer.reset();
        overlay_renderer.reset();
        basic_renderer.reset();
        opengl.reset();
        window.reset();
        events.reset();
        time.reset();
        settings.reset();
        g_current_engine = nullptr;
    }

    void engine::init()
    {
        // Matches the old main_loop.cpp init sequence: events,
        // rendering, scene graph. The window/GL context is brought up
        // inside rendering_engine::context::init(); it in turn
        // constructs the two built-in renderers once GL is alive.
        events->init();

        // Game modules self-register at static-init time (before the
        // engine existed) — wire those queued registrations onto the
        // now-live event bus before anything broadcasts.
        install_pending_game_modules();

        renderer->init();
        scenes->init();

        // Register our own quit_requested listener now that the event
        // bus is initialised.
        events->register_listener(event_engine::event_type::quit_requested,
                                  [this](const event_engine::event&) { m_quit_requested = true; });
    }

    void engine::quit()
    {
        scenes->quit();
        renderer->quit();
        events->quit();
    }

    void engine::tick()
    {
        window->tick();
        renderer->render();
        window->swap_buffers();
        time->perform_tick();
    }

    void engine::broadcast_engine_start()
    {
        events->broadcast(event_engine::engine_start());
    }

    void engine::broadcast_engine_stop()
    {
        events->broadcast(event_engine::engine_stop());
    }

    bool engine::is_quit_requested() const noexcept
    {
        return m_quit_requested;
    }
} // namespace control
