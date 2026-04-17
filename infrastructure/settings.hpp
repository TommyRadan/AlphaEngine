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
 * @file settings.hpp
 * @brief Global read-only application settings (window, camera, input).
 */

#pragma once

#include <string>

#include <infrastructure/singleton.hpp>

/** @brief Presentation mode for the application window. */
enum class win_type
{
    win_type_windowed,   /**< Standard decorated window. */
    win_type_borderless, /**< Borderless window. */
    win_type_fullscreen  /**< Exclusive fullscreen. */
};

/**
 * @brief Process-wide configuration singleton.
 *
 * Values are populated in the constructor — debug builds default to an
 * 800x600 windowed layout, release builds match the current display
 * mode and go fullscreen. All accessors are @c const and @c noexcept;
 * the struct is intended as read-only engine-wide configuration.
 */
struct settings : public singleton<settings>
{
    settings();

    const unsigned int get_window_width() const noexcept;
    const unsigned int get_window_height() const noexcept;
    const char* get_window_name() const noexcept;
    const win_type get_window_type() const noexcept;
    const bool is_double_buffered() const noexcept;
    const float get_field_of_view() const noexcept;
    const float get_mouse_sensitivity() const noexcept;

    /** @brief Returns @c width / @c height of the window. */
    const float get_aspect_ratio() const noexcept;

    const bool is_mouse_reversed() const noexcept;

private:
    unsigned int m_window_width;
    unsigned int m_window_height;
    std::string m_window_name;
    win_type m_window_type;
    bool m_is_double_buffered;
    float m_field_of_view;
    float m_mouse_sensitivity;
    bool m_is_mouse_reversed;
};
