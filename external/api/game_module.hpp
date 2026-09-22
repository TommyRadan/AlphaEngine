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

#include <functional>

#include <core/event_engine.hpp>

struct game_module_info
{
    std::function<void(const core::engine_start&)> on_engine_start;
    std::function<void(const core::engine_stop&)> on_engine_stop;
    std::function<void(const core::frame&)> on_frame;
    std::function<void(const core::render_update&)> on_render_update;
    std::function<void(const core::render_scene&)> on_render_scene;
    std::function<void(const core::render_ui&)> on_render_ui;
    std::function<void(const core::key_down&)> on_key_down;
    std::function<void(const core::key_up&)> on_key_up;
    std::function<void(const core::mouse_key_down&)> on_mouse_key_down;
    std::function<void(const core::mouse_key_up&)> on_mouse_key_up;
    std::function<void(const core::mouse_move&)> on_mouse_move;
    std::function<void(const core::mouse_wheel&)> on_mouse_wheel;
    std::function<void(const core::text_input&)> on_text_input;
    std::function<void(const core::window_resized&)> on_window_resized;
    std::function<void(const core::window_focus&)> on_window_focus;
    std::function<void(const core::window_minimized&)> on_window_minimized;
    std::function<void(const core::gamepad_connected&)> on_gamepad_connected;
    std::function<void(const core::gamepad_disconnected&)> on_gamepad_disconnected;
    std::function<void(const core::gamepad_button&)> on_gamepad_button;
    std::function<void(const core::gamepad_axis&)> on_gamepad_axis;

    game_module_info()
        : on_engine_start{nullptr}, on_engine_stop{nullptr}, on_frame{nullptr}, on_render_update{nullptr},
          on_render_scene{nullptr}, on_render_ui{nullptr}, on_key_down{nullptr}, on_key_up{nullptr},
          on_mouse_key_down{nullptr}, on_mouse_key_up{nullptr}, on_mouse_move{nullptr}, on_mouse_wheel{nullptr},
          on_text_input{nullptr}, on_window_resized{nullptr}, on_window_focus{nullptr}, on_window_minimized{nullptr},
          on_gamepad_connected{nullptr}, on_gamepad_disconnected{nullptr}, on_gamepad_button{nullptr},
          on_gamepad_axis{nullptr}
    {
    }
};

void register_game_module(const game_module_info& info);

#ifdef INTERNAL_GAMEMODULE_IMPLEMENTATION
// Engine-side hook, deliberately kept out of the module-facing surface (a
// module translation unit gets the GAME_MODULE() block below instead).
// runtime::engine::init calls it once the event bus is live to wire every
// registration queued at static-init time onto the bus.
void install_pending_game_modules();
#else
#define GAME_MODULE() static bool module_init()
static bool module_init();
static bool init_status = module_init();
#endif
