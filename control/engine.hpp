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

/**
 * @file engine.hpp
 * @brief Central, owning container for every engine subsystem.
 *
 * Replaces the process-wide @c singleton<T> pattern that used to live
 * under @c infrastructure/singleton.hpp. @ref engine owns each subsystem
 * as a @c std::unique_ptr so construction/destruction order is explicit
 * and deterministic, and multiple instances are possible (e.g. from
 * tests). One live instance is published through @ref current_engine so
 * existing call sites that reached for a global can resolve the
 * subsystem they need without threading an @c engine& through every
 * function.
 */

#pragma once

#include <memory>

// Forward declarations keep this header lightweight. Subsystem headers
// are included only in engine.cpp where the unique_ptrs are constructed
// and destroyed.
struct settings;
namespace event_engine
{
    struct context;
}
namespace infrastructure
{
    struct time;
}
namespace rendering_engine
{
    struct window;
    namespace opengl
    {
        struct context;
    }
    namespace renderers
    {
        struct basic_renderer;
        struct overlay_renderer;
    } // namespace renderers
    struct context;
} // namespace rendering_engine
namespace scene_graph
{
    struct context;
}

namespace control
{
    /**
     * @brief Owning container for every engine subsystem.
     *
     * The constructor wires up each subsystem and installs itself as
     * the process-wide @ref current_engine; the destructor tears
     * subsystems down in reverse and clears the current-engine pointer.
     * Not copyable and not moveable — the engine publishes its address
     * through @ref current_engine and moving would invalidate that
     * pointer.
     *
     * All methods are main-thread-only. Member access is not
     * synchronised.
     */
    struct engine
    {
        engine();
        ~engine();

        engine(const engine&) = delete;
        engine& operator=(const engine&) = delete;
        engine(engine&&) = delete;
        engine& operator=(engine&&) = delete;

        /** @brief Initializes every subsystem in dependency order. */
        void init();

        /** @brief Tears every subsystem down in reverse order. */
        void quit();

        /**
         * @brief Runs one iteration of the main loop: pumps input,
         *        renders one frame, advances the clock.
         */
        void tick();

        /** @brief Broadcasts @c engine_start on the event bus. */
        void broadcast_engine_start();

        /** @brief Broadcasts @c engine_stop on the event bus. */
        void broadcast_engine_stop();

        /** @brief Returns true when a @c quit_requested event has been observed. */
        bool is_quit_requested() const noexcept;

        // Subsystems. Owned as unique_ptr so lifetime mirrors the
        // engine's own lifetime, in the order they are declared here.
        std::unique_ptr<::settings> settings;
        std::unique_ptr<infrastructure::time> time;
        std::unique_ptr<event_engine::context> events;
        std::unique_ptr<rendering_engine::window> window;
        std::unique_ptr<rendering_engine::opengl::context> opengl;
        std::unique_ptr<rendering_engine::renderers::basic_renderer> basic_renderer;
        std::unique_ptr<rendering_engine::renderers::overlay_renderer> overlay_renderer;
        std::unique_ptr<rendering_engine::context> renderer;
        std::unique_ptr<scene_graph::context> scenes;

    private:
        bool m_quit_requested{false};
    };

    /**
     * @brief Returns the currently-live @ref engine instance.
     *
     * Undefined behaviour if called when no engine is constructed. The
     * pointer is installed by the @ref engine constructor and cleared
     * by its destructor; tests that construct their own @c engine on
     * the stack therefore get a well-defined value here for the
     * duration of the test.
     */
    engine& current_engine();
} // namespace control
