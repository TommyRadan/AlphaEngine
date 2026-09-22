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

#include <rendering_engine/sdl_input.hpp>

#include <algorithm>

#include <SDL3/SDL_mouse.h>

namespace rendering_engine::sdl_input
{
    core::key_code to_key_code(SDL_Keycode key) noexcept
    {
        using core::key_code;

        switch (key)
        {
        case SDLK_A:
            return key_code::a;
        case SDLK_B:
            return key_code::b;
        case SDLK_C:
            return key_code::c;
        case SDLK_D:
            return key_code::d;
        case SDLK_E:
            return key_code::e;
        case SDLK_F:
            return key_code::f;
        case SDLK_G:
            return key_code::g;
        case SDLK_H:
            return key_code::h;
        case SDLK_I:
            return key_code::i;
        case SDLK_J:
            return key_code::j;
        case SDLK_K:
            return key_code::k;
        case SDLK_L:
            return key_code::l;
        case SDLK_M:
            return key_code::m;
        case SDLK_N:
            return key_code::n;
        case SDLK_O:
            return key_code::o;
        case SDLK_P:
            return key_code::p;
        case SDLK_Q:
            return key_code::q;
        case SDLK_R:
            return key_code::r;
        case SDLK_S:
            return key_code::s;
        case SDLK_T:
            return key_code::t;
        case SDLK_U:
            return key_code::u;
        case SDLK_V:
            return key_code::v;
        case SDLK_W:
            return key_code::w;
        case SDLK_X:
            return key_code::x;
        case SDLK_Y:
            return key_code::y;
        case SDLK_Z:
            return key_code::z;

        case SDLK_0:
            return key_code::num_0;
        case SDLK_1:
            return key_code::num_1;
        case SDLK_2:
            return key_code::num_2;
        case SDLK_3:
            return key_code::num_3;
        case SDLK_4:
            return key_code::num_4;
        case SDLK_5:
            return key_code::num_5;
        case SDLK_6:
            return key_code::num_6;
        case SDLK_7:
            return key_code::num_7;
        case SDLK_8:
            return key_code::num_8;
        case SDLK_9:
            return key_code::num_9;

        case SDLK_F1:
            return key_code::f1;
        case SDLK_F2:
            return key_code::f2;
        case SDLK_F3:
            return key_code::f3;
        case SDLK_F4:
            return key_code::f4;
        case SDLK_F5:
            return key_code::f5;
        case SDLK_F6:
            return key_code::f6;
        case SDLK_F7:
            return key_code::f7;
        case SDLK_F8:
            return key_code::f8;
        case SDLK_F9:
            return key_code::f9;
        case SDLK_F10:
            return key_code::f10;
        case SDLK_F11:
            return key_code::f11;
        case SDLK_F12:
            return key_code::f12;

        case SDLK_RETURN:
            return key_code::enter;
        case SDLK_ESCAPE:
            return key_code::escape;
        case SDLK_BACKSPACE:
            return key_code::backspace;
        case SDLK_TAB:
            return key_code::tab;
        case SDLK_SPACE:
            return key_code::space;
        case SDLK_INSERT:
            return key_code::insert;
        case SDLK_DELETE:
            return key_code::del;
        case SDLK_HOME:
            return key_code::home;
        case SDLK_END:
            return key_code::end;
        case SDLK_PAGEUP:
            return key_code::page_up;
        case SDLK_PAGEDOWN:
            return key_code::page_down;

        case SDLK_LEFT:
            return key_code::left;
        case SDLK_RIGHT:
            return key_code::right;
        case SDLK_UP:
            return key_code::up;
        case SDLK_DOWN:
            return key_code::down;

        case SDLK_LSHIFT:
            return key_code::left_shift;
        case SDLK_RSHIFT:
            return key_code::right_shift;
        case SDLK_LCTRL:
            return key_code::left_ctrl;
        case SDLK_RCTRL:
            return key_code::right_ctrl;
        case SDLK_LALT:
            return key_code::left_alt;
        case SDLK_RALT:
            return key_code::right_alt;
        case SDLK_LGUI:
            return key_code::left_gui;
        case SDLK_RGUI:
            return key_code::right_gui;

        case SDLK_CAPSLOCK:
            return key_code::caps_lock;
        case SDLK_NUMLOCKCLEAR:
            return key_code::num_lock;
        case SDLK_SCROLLLOCK:
            return key_code::scroll_lock;
        case SDLK_PRINTSCREEN:
            return key_code::print_screen;
        case SDLK_PAUSE:
            return key_code::pause;
        case SDLK_APPLICATION:
            return key_code::menu;

        case SDLK_MINUS:
            return key_code::minus;
        case SDLK_EQUALS:
            return key_code::equals;
        case SDLK_LEFTBRACKET:
            return key_code::left_bracket;
        case SDLK_RIGHTBRACKET:
            return key_code::right_bracket;
        case SDLK_BACKSLASH:
            return key_code::backslash;
        case SDLK_SEMICOLON:
            return key_code::semicolon;
        case SDLK_APOSTROPHE:
            return key_code::apostrophe;
        case SDLK_GRAVE:
            return key_code::grave;
        case SDLK_COMMA:
            return key_code::comma;
        case SDLK_PERIOD:
            return key_code::period;
        case SDLK_SLASH:
            return key_code::slash;

        case SDLK_KP_0:
            return key_code::keypad_0;
        case SDLK_KP_1:
            return key_code::keypad_1;
        case SDLK_KP_2:
            return key_code::keypad_2;
        case SDLK_KP_3:
            return key_code::keypad_3;
        case SDLK_KP_4:
            return key_code::keypad_4;
        case SDLK_KP_5:
            return key_code::keypad_5;
        case SDLK_KP_6:
            return key_code::keypad_6;
        case SDLK_KP_7:
            return key_code::keypad_7;
        case SDLK_KP_8:
            return key_code::keypad_8;
        case SDLK_KP_9:
            return key_code::keypad_9;
        case SDLK_KP_DIVIDE:
            return key_code::keypad_divide;
        case SDLK_KP_MULTIPLY:
            return key_code::keypad_multiply;
        case SDLK_KP_MINUS:
            return key_code::keypad_minus;
        case SDLK_KP_PLUS:
            return key_code::keypad_plus;
        case SDLK_KP_ENTER:
            return key_code::keypad_enter;
        case SDLK_KP_PERIOD:
            return key_code::keypad_period;
        case SDLK_KP_EQUALS:
            return key_code::keypad_equals;

        default:
            return key_code::unknown;
        }
    }

    std::optional<core::mouse_key_code> to_mouse_key_code(std::uint8_t button) noexcept
    {
        switch (button)
        {
        case SDL_BUTTON_LEFT:
            return core::mouse_key_code::left;
        case SDL_BUTTON_RIGHT:
            return core::mouse_key_code::right;
        case SDL_BUTTON_MIDDLE:
            return core::mouse_key_code::middle;
        case SDL_BUTTON_X1:
            return core::mouse_key_code::x1;
        case SDL_BUTTON_X2:
            return core::mouse_key_code::x2;
        default:
            return std::nullopt;
        }
    }

    core::gamepad_button_code to_gamepad_button_code(SDL_GamepadButton button) noexcept
    {
        using core::gamepad_button_code;

        switch (button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return gamepad_button_code::south;
        case SDL_GAMEPAD_BUTTON_EAST:
            return gamepad_button_code::east;
        case SDL_GAMEPAD_BUTTON_WEST:
            return gamepad_button_code::west;
        case SDL_GAMEPAD_BUTTON_NORTH:
            return gamepad_button_code::north;
        case SDL_GAMEPAD_BUTTON_BACK:
            return gamepad_button_code::back;
        case SDL_GAMEPAD_BUTTON_GUIDE:
            return gamepad_button_code::guide;
        case SDL_GAMEPAD_BUTTON_START:
            return gamepad_button_code::start;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return gamepad_button_code::left_stick;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return gamepad_button_code::right_stick;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return gamepad_button_code::left_shoulder;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return gamepad_button_code::right_shoulder;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return gamepad_button_code::dpad_up;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return gamepad_button_code::dpad_down;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return gamepad_button_code::dpad_left;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return gamepad_button_code::dpad_right;
        case SDL_GAMEPAD_BUTTON_MISC1:
            return gamepad_button_code::misc1;
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:
            return gamepad_button_code::right_paddle1;
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:
            return gamepad_button_code::left_paddle1;
        case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:
            return gamepad_button_code::right_paddle2;
        case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:
            return gamepad_button_code::left_paddle2;
        case SDL_GAMEPAD_BUTTON_TOUCHPAD:
            return gamepad_button_code::touchpad;
        case SDL_GAMEPAD_BUTTON_MISC2:
            return gamepad_button_code::misc2;
        case SDL_GAMEPAD_BUTTON_MISC3:
            return gamepad_button_code::misc3;
        case SDL_GAMEPAD_BUTTON_MISC4:
            return gamepad_button_code::misc4;
        case SDL_GAMEPAD_BUTTON_MISC5:
            return gamepad_button_code::misc5;
        case SDL_GAMEPAD_BUTTON_MISC6:
            return gamepad_button_code::misc6;
        default:
            return gamepad_button_code::unknown;
        }
    }

    core::gamepad_axis_code to_gamepad_axis_code(SDL_GamepadAxis axis) noexcept
    {
        using core::gamepad_axis_code;

        switch (axis)
        {
        case SDL_GAMEPAD_AXIS_LEFTX:
            return gamepad_axis_code::left_x;
        case SDL_GAMEPAD_AXIS_LEFTY:
            return gamepad_axis_code::left_y;
        case SDL_GAMEPAD_AXIS_RIGHTX:
            return gamepad_axis_code::right_x;
        case SDL_GAMEPAD_AXIS_RIGHTY:
            return gamepad_axis_code::right_y;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return gamepad_axis_code::left_trigger;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return gamepad_axis_code::right_trigger;
        default:
            return gamepad_axis_code::unknown;
        }
    }

    float normalize_axis(std::int16_t value) noexcept
    {
        // The raw range is asymmetric (-32768..32767); divide by the positive
        // extreme and clamp so both ends land exactly on the unit bounds.
        constexpr float k_axis_max = 32767.0f;
        return std::clamp(static_cast<float>(value) / k_axis_max, -1.0f, 1.0f);
    }
} // namespace rendering_engine::sdl_input
