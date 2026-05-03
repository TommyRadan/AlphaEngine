/**
 * Copyright (c) 2015-2019 Tomislav Radanovic
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
 * @file rendering_engine.hpp
 * @brief Top-level entry point for the rendering subsystem.
 */

#pragma once

#include <memory>
#include <vector>

namespace rendering_engine
{
    struct pass;
    struct renderable;

    /**
     * @brief Orchestrates the rendering subsystem (window, GL context, renderers).
     *
     * Owned by @ref control::engine. @ref init brings up the window
     * and OpenGL context and constructs the built-in renderers;
     * @ref quit tears the GL context and window down in reverse order.
     * All methods must be called from the main thread that owns the GL
     * context.
     */
    struct context
    {
        context();
        // Defined out-of-line in rendering_engine.cpp so the
        // std::vector<std::unique_ptr<pass>> destructor is only
        // instantiated where @ref pass is a complete type. The
        // header keeps @c pass forward-declared.
        ~context();

        /**
         * @brief Initializes the window, GL context and built-in renderers.
         *        Must be called once before @ref render.
         */
        void init();

        /** @brief Tears the renderers, GL context and window down. */
        void quit();

        /**
         * @brief Renders one frame.
         *
         * Walks the ordered pass list registered in @ref init,
         * giving each pass the same per-frame @ref frame_context
         * (active camera + swapchain target) so they cannot
         * disagree mid-frame. The built-in scene and UI passes
         * each broadcast their matching event
         * (@ref event_engine::render_scene / @ref event_engine::render_ui)
         * after the registry walk so debug / gizmo callers can
         * still subscribe. Does not swap buffers — callers are
         * responsible for presenting.
         */
        void render();

        /**
         * @brief Adds @p r to the scene-pass registry.
         *
         * The pointer is non-owning; callers must @ref unregister_scene_renderable
         * before destroying the renderable. Registration order is
         * preserved and is the dispatch order during the scene pass.
         */
        void register_scene_renderable(renderable* r);

        /** @brief Removes @p r from the scene-pass registry; no-op if absent. */
        void unregister_scene_renderable(renderable* r);

        /** @brief Adds @p r to the UI-pass registry; same ownership rules as the scene variant. */
        void register_ui_renderable(renderable* r);

        /** @brief Removes @p r from the UI-pass registry; no-op if absent. */
        void unregister_ui_renderable(renderable* r);

    private:
        std::vector<renderable*> m_scene_renderables;
        std::vector<renderable*> m_ui_renderables;

        // Ordered pass list walked once per frame in @ref render.
        // Populated by @ref init with the built-in scene + UI passes
        // and torn down first in @ref quit, before the renderers and
        // GPU device the passes reference.
        std::vector<std::unique_ptr<pass>> m_passes;
    };
} // namespace rendering_engine
