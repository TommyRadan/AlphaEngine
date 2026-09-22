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

/**
 * @file sdl_input.hpp
 * @brief Pure translation of SDL input identifiers into the engine's
 *        @ref core::key_code / mouse / gamepad enums.
 *
 * Kept apart from @ref rendering_engine::window so the mapping needs no
 * window or video subsystem and can be unit-tested headless. Every
 * function is total: an SDL value the engine has no name for maps to the
 * matching @c unknown enumerator (or @c std::nullopt for mouse buttons)
 * rather than being cast through.
 */

#pragma once

#include <cstdint>
#include <optional>

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_keycode.h>

#include <core/event.hpp>

namespace rendering_engine::sdl_input
{
    /**
     * @brief Maps an SDL keycode (@c SDL_KeyboardEvent::key) to the engine
     *        key. Keys without an engine name yield @ref core::key_code::unknown.
     */
    core::key_code to_key_code(SDL_Keycode key) noexcept;

    /**
     * @brief Maps an SDL mouse button index (@c SDL_BUTTON_LEFT ...) to the
     *        engine button, or @c std::nullopt for a button the engine does
     *        not name — callers skip the event rather than emit a guess.
     */
    std::optional<core::mouse_key_code> to_mouse_key_code(std::uint8_t button) noexcept;

    /**
     * @brief Maps an SDL gamepad button (@c SDL_GamepadButtonEvent::button)
     *        to the engine button; unnamed buttons yield
     *        @ref core::gamepad_button_code::unknown.
     */
    core::gamepad_button_code to_gamepad_button_code(SDL_GamepadButton button) noexcept;

    /**
     * @brief Maps an SDL gamepad axis (@c SDL_GamepadAxisEvent::axis) to the
     *        engine axis; unnamed axes yield @ref core::gamepad_axis_code::unknown.
     */
    core::gamepad_axis_code to_gamepad_axis_code(SDL_GamepadAxis axis) noexcept;

    /**
     * @brief Normalises a raw SDL axis reading (-32768..32767) to [-1, 1].
     *        Both extremes map to exactly -1 / +1 and zero stays zero, so
     *        trigger axes (0..32767) land in [0, 1].
     */
    float normalize_axis(std::int16_t value) noexcept;
} // namespace rendering_engine::sdl_input
