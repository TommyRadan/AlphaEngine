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

#include <event_engine/event_engine.hpp>
#include <string>

struct game_module_info
{
    std::function<void(const event_engine::event&)> on_engine_start;
    std::function<void(const event_engine::event&)> on_engine_stop;
    std::function<void(const event_engine::event&)> on_frame;
    std::function<void(const event_engine::event&)> on_render_scene;
    std::function<void(const event_engine::event&)> on_render_ui;
    std::function<void(const event_engine::event&)> on_key_down;
    std::function<void(const event_engine::event&)> on_key_up;
    std::function<void(const event_engine::event&)> on_mouse_key_down;
    std::function<void(const event_engine::event&)> on_mouse_key_up;
    std::function<void(const event_engine::event&)> on_mouse_move;

    game_module_info()
        : on_engine_start{nullptr}, on_engine_stop{nullptr}, on_frame{nullptr}, on_render_scene{nullptr}, on_render_ui{nullptr},
          on_key_down{nullptr}, on_key_up{nullptr}, on_mouse_key_down{nullptr}, on_mouse_key_up{nullptr}, on_mouse_move{nullptr}
    {
    }
};

void register_game_module(struct game_module_info& info);

#ifndef INTERNAL_GAMEMODULE_IMPLEMENTATION
#define GAME_MODULE() static bool module_init()
static bool module_init();
static bool init_status = module_init();
#endif
