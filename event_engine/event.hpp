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
 * @brief Event value types broadcast through the event engine.
 */

#pragma once

namespace event_engine
{
    /**
     * @brief Discriminator used by @ref event to identify its dynamic type
     *        and by listeners to subscribe to a specific event category.
     */
    enum class event_type
    {
        engine_start,
        engine_stop,
        quit_requested,
        frame,
        render_scene,
        render_ui,
        mouse_key_up,
        mouse_key_down,
        key_up,
        key_down,
        mouse_move
    };

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

    /**
     * @brief Polymorphic base for every event passed through the bus.
     *
     * Listeners receive a @c const reference and may @c static_cast /
     * @c dynamic_cast to the concrete subtype after inspecting @ref m_type.
     * Events are passed by reference only and are not retained by the
     * dispatcher — derived objects are typically stack-allocated at the
     * broadcast site.
     */
    struct event
    {
        event(event_type type) : m_type{type} {}
        virtual ~event() = default;

        event_type m_type;
    };

    /** @brief Broadcast once after all subsystems have been initialized. */
    struct engine_start : public event
    {
        engine_start() : event(event_type::engine_start) {}
    };

    /** @brief Broadcast once as the main loop is tearing down. */
    struct engine_stop : public event
    {
        engine_stop() : event(event_type::engine_stop) {}
    };

    /** @brief Signals the user requested application termination. */
    struct quit_requested : public event
    {
        quit_requested() : event(event_type::quit_requested) {}
    };

    /**
     * @brief Per-frame tick event.
     *
     * Broadcast once per iteration of the main loop and carries the
     * time elapsed since the previous frame.
     */
    struct frame : public event
    {
        frame() : event(event_type::frame) {}

        /** @brief Time since the previous frame, in milliseconds. */
        float m_delta_time;
    };

    /** @brief Broadcast while the 3D scene pass is active; renderables should submit draw calls. */
    struct render_scene : public event
    {
        render_scene() : event(event_type::render_scene) {}
    };

    /** @brief Broadcast while the 2D overlay/UI pass is active. */
    struct render_ui : public event
    {
        render_ui() : event(event_type::render_ui) {}
    };

    /** @brief Key release. @ref m_key_code is the released key. */
    struct key_up : public event
    {
        key_up() : event(event_type::key_up) {}

        key_code m_key_code;
    };

    /** @brief Key press. @ref m_key_code is the pressed key. */
    struct key_down : public event
    {
        key_down() : event(event_type::key_down) {}

        key_code m_key_code;
    };

    /** @brief Mouse button release. @ref m_key_code is the released button. */
    struct mouse_key_up : public event
    {
        mouse_key_up() : event(event_type::mouse_key_up) {}

        mouse_key_code m_key_code;
    };

    /** @brief Mouse button press. @ref m_key_code is the pressed button. */
    struct mouse_key_down : public event
    {
        mouse_key_down() : event(event_type::mouse_key_down) {}

        mouse_key_code m_key_code;
    };

    /**
     * @brief Mouse motion event.
     *
     * @ref m_x and @ref m_y carry the relative motion since the previous
     * report (not absolute cursor coordinates).
     */
    struct mouse_move : public event
    {
        mouse_move() : event(event_type::mouse_move) {}

        int m_x; /**< Horizontal delta in pixels. */
        int m_y; /**< Vertical delta in pixels. */
    };
} // namespace event_engine
