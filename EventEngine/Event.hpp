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

#pragma once

namespace EventEngine
{
    struct engine_start : public Event
    {
        engine_start() {}
    };

    struct engine_stop : public Event
    {
        engine_stop() {}
    };

    struct script_start : public Event
    {
        script_start() {}
    };

    struct script_stop : public Event
    {
        script_stop() {}
    };

    struct frame : public Event
    {
        frame() {}

        float m_deltaTime;
    };

    struct key_up : public Event
    {
        key_up() {}

        int m_keyCode;
    };

    struct key_down : public Event
    {
        key_down() {}

        int m_keyCode;
    };

    struct mouse_move : public Event
    {
        mouse_move() {}

        int m_x;
        int m_y;
    };

    struct event
    {
        event() = default;
    };
}
