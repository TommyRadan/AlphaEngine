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

#include <infrastructure/singleton.hpp>
#include <rhi/rhi.hpp>

namespace rendering_engine
{
    /**
     * @brief Orchestrates the rendering subsystem (window, RHI device, renderers).
     *
     * Process-wide singleton. @ref init brings up the window and RHI
     * backend and constructs the built-in renderers; @ref quit tears the
     * RHI device and window down in reverse order. All methods must be
     * called from the main thread that owns the graphics context.
     */
    struct context : public singleton<context>
    {
        /**
         * @brief Initializes the window, RHI backend and built-in renderers.
         *        Must be called once before @ref render.
         */
        void init();

        /** @brief Tears the renderers, RHI device and window down. */
        void quit();

        /**
         * @brief Renders one frame.
         *
         * Runs the scene pass (broadcasting @ref event_engine::render_scene
         * if a camera is active) followed by the UI overlay pass
         * (broadcasting @ref event_engine::render_ui). Depth testing is
         * disabled for the overlay and restored afterwards. Does not
         * swap buffers — callers are responsible for presenting.
         */
        void render();

    private:
        std::unique_ptr<rhi::device> m_rhi_device;
    };
} // namespace rendering_engine
