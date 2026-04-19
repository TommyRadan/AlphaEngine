/**
 * Copyright (c) 2015-2025 Tomislav Radanovic
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
 * @file event.hpp
 * @brief Built-in event value types broadcast through the event bus.
 *
 * Every event is an ordinary value type — there is no shared base class.
 * The @ref event_engine::event_bus is keyed on @c std::type_index, so game
 * modules can define their own event structs and dispatch them through
 * the same bus without modifying this header.
 */

#pragma once

namespace event_engine
{
    /**
     * @brief Keyboard key identifiers. Values mirror SDL keysym codes so
     *        the window layer can cast directly into this enum.
     */
    enum class key_code
    {
        w = 119,
        a = 97,
        s = 115,
        d = 100,
        space = 32,
        shift = 1073742049,
        ctrl = 1073741881,
        enter = 13,
        escape = 27,
    };

    /** @brief Mouse button identifiers. */
    enum class mouse_key_code
    {
        left = 141881,
        right = 141882,
        middle = 141883,
    };

    /** @brief Broadcast once after all subsystems have been initialized. */
    struct engine_start
    {
    };

    /** @brief Broadcast once as the main loop is tearing down. */
    struct engine_stop
    {
    };

    /** @brief Signals the user requested application termination. */
    struct quit_requested
    {
    };

    /**
     * @brief Per-frame tick event.
     *
     * Broadcast once per iteration of the main loop and carries the
     * time elapsed since the previous frame.
     */
    struct frame
    {
        /** @brief Time since the previous frame, in milliseconds. */
        float m_delta_time;
    };

    /** @brief Broadcast while the 3D scene pass is active; renderables should submit draw calls. */
    struct render_scene
    {
    };

    /** @brief Broadcast while the 2D overlay/UI pass is active. */
    struct render_ui
    {
    };

    /** @brief Key release. @ref m_key_code is the released key. */
    struct key_up
    {
        key_code m_key_code;
    };

    /** @brief Key press. @ref m_key_code is the pressed key. */
    struct key_down
    {
        key_code m_key_code;
    };

    /** @brief Mouse button release. @ref m_key_code is the released button. */
    struct mouse_key_up
    {
        mouse_key_code m_key_code;
    };

    /** @brief Mouse button press. @ref m_key_code is the pressed button. */
    struct mouse_key_down
    {
        mouse_key_code m_key_code;
    };

    /**
     * @brief Mouse motion event.
     *
     * @ref m_x and @ref m_y carry the relative motion since the previous
     * report (not absolute cursor coordinates).
     */
    struct mouse_move
    {
        int m_x; /**< Horizontal delta in pixels. */
        int m_y; /**< Vertical delta in pixels. */
    };
} // namespace event_engine
