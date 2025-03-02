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

#pragma once

namespace event_engine
{
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

    enum class key_code
    {
        W = 119,
        A = 97,
        S = 115,
        D = 100,
        SPACE = 32,
        SHIFT = 1073742049,
        CTRL = 1073741881,
        ENTER = 13,
        ESCAPE = 27,
    };

    enum class mouse_key_code
    {
        LEFT = 141881,
        RIGHT = 141882,
        MIDDLE = 141883,
    };

    struct event
    {
        event(event_type type) : m_type { type } {}
        virtual ~event() = default;

        event_type m_type;
    };

    struct engine_start : public event
    {
        engine_start() : event(event_type::engine_start) {}
    };

    struct engine_stop : public event
    {
        engine_stop() : event(event_type::engine_stop) {}
    };

    struct quit_requested : public event
    {
        quit_requested() : event(event_type::quit_requested) {}
    };

    struct frame : public event
    {
        frame() : event(event_type::frame) {}

        float m_delta_time;
    };

    struct render_scene : public event
    {
        render_scene() : event(event_type::render_scene) {}
    };

    struct render_ui : public event
    {
        render_ui() : event(event_type::render_ui) {}
    };

    struct key_up : public event
    {
        key_up() : event(event_type::key_up) {}

        key_code m_key_code;
    };

    struct key_down : public event
    {
        key_down() : event(event_type::key_down) {}

        key_code m_key_code;
    };

    struct mouse_key_up : public event
    {
        mouse_key_up() : event(event_type::mouse_key_up) {}

        mouse_key_code m_key_code;
    };

    struct mouse_key_down : public event
    {
        mouse_key_down() : event(event_type::mouse_key_down) {}

        mouse_key_code m_key_code;
    };

    struct mouse_move : public event
    {
        mouse_move() : event(event_type::mouse_move) {}

        int m_x;
        int m_y;
    };
}
