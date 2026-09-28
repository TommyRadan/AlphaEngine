// SPDX-License-Identifier: MIT
// Copyright (c) 2015-2026 Tomislav Radanovic

/**
 * @file overlay.hpp
 * @brief @ref runtime::overlay — a tool layer drawn over the engine's
 *        frames, which the executable installs on the engine.
 */

#pragma once

namespace runtime
{
    struct engine;

    /**
     * @brief A tool layer drawn over every rendered frame, such as the
     *        debug editor.
     *
     * The executable hands one to @ref engine::set_overlay before
     * @ref engine::init, so the engine drives a layer that is built on top
     * of it without depending on it. The engine calls @ref init once the
     * renderer is up; once per rendered frame, @ref begin_frame after the
     * scenes have updated and before the render extraction, and
     * @ref end_frame right after the extraction, before the renderer
     * records; and @ref shutdown before the renderer goes. Main-thread
     * only.
     */
    struct overlay
    {
        virtual ~overlay() = default;

        /**
         * @brief Brings the overlay up against @p eng, whose window, GPU
         *        device and renderer are live. A failure is logged and
         *        leaves the overlay inert; it does not stop the engine.
         */
        virtual void init(engine& eng) = 0;

        /**
         * @brief Starts this frame's overlay, before the render extraction,
         *        so what it changes in the scenes reaches this frame.
         */
        virtual void begin_frame() = 0;

        /**
         * @brief Finishes this frame's overlay once the render extraction has
         *        written this frame's proxies, which it may read (to draw
         *        gizmos through the debug-draw functions, say); the renderer
         *        records the result with the frame.
         */
        virtual void end_frame() = 0;

        /**
         * @brief Takes the overlay down while the window, the GPU device
         *        and the renderer are still up. Safe to call when @ref init
         *        never ran or failed.
         */
        virtual void shutdown() = 0;
    };
} // namespace runtime
